# GraphBridge AI - graphbridge_concurrency_test.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
Concurrency tests for the v2.0.1 per-connection auth fix
(GraphBridgeAutomationLibrary.cpp: FGraphBridgeConnectionState +
setConnectionStateFactory / setOnConnectionCallback).

Tests/graphbridge_handshake_security_test.py is real and passing, but it is
SEQUENTIAL: one client, one connection, no contention. It proves the auth
logic is correct in isolation. It does not prove the *state* the logic reads
is correctly scoped per-connection under concurrent load -- which is exactly
what the pre-fix TSet<ix::WebSocket*> design got wrong (address identity is
not connection identity once sockets can be created and destroyed while
others are live).

This file adds the tests that actually contend:

  1. concurrent_mixed_auth   - 32 simultaneous clients (16 valid token, 16
                                bad/absent), each hammering the server for
                                10s. Zero unauthenticated executions, all
                                authenticated executions succeed, no crash.
  2. churn_address_reuse     - one thread rapidly connect+command+disconnect
                                (x1000) with a VALID token, to maximize the
                                chance IXWebSocket's allocator reuses a freed
                                connection's memory, while a second thread
                                concurrently hammers the server with NO
                                token. Zero unauthenticated executions.
  3. disconnect_before_reply - send a command, close the socket before any
                                response arrives, x200. No crash, no ensure.
  4. buffered_pre_auth_frame - the existing burst-race case from
                                graphbridge_handshake_security_test.py,
                                folded into this run so one invocation
                                exercises all four.

Measurement is SERVER-SIDE, not by absence of a client response: a dropped
reply looks identical to "never executed" from the client's side, but they
are different failure modes -- "executed, reply silently dropped" is exactly
the kind of bug this fix could reintroduce and a response-based test would
miss. Every command frame this file sends carries a unique marker; pass/fail
is decided by grepping the *server's own log* for how many times each marker
was actually dispatched (GraphBridgeAutomationLibrary.cpp's
"GraphBridge [%s] received: %s %s" line in ExecuteAtomicCommand -- Verbose
level, so LogGraphBridge must be raised to Verbose before running this file:
run `Log LogGraphBridge Verbose` in the editor's console, or launch with
`-LogCmds="LogGraphBridge Verbose"`).

Usage:
    python graphbridge_concurrency_test.py [--log PATH_TO_EDITOR_LOG]

Requires: the GraphBridge server running on 127.0.0.1:8080 with LogGraphBridge
raised to Verbose (see above), and the editor's live Output Log accessible.
"""

import argparse
import base64
import glob
import os
import random
import re
import socket
import struct
import sys
import threading
import time
import uuid

HOST = "127.0.0.1"
PORT = 8080


# ---------------------------------------------------------------------------
# Low-level WS framing (same technique as graphbridge_handshake_security_test.py:
# hand-rolled so we can send a command in the same TCP burst as the handshake,
# or omit/mangle the handshake token, which no high-level `websockets` client
# API lets you do cleanly).
# ---------------------------------------------------------------------------

def make_sec_websocket_key() -> str:
    return base64.b64encode(bytes(random.getrandbits(8) for _ in range(16))).decode("ascii")


def build_ws_text_frame(payload: str) -> bytes:
    """Hand-rolled RFC 6455 client frame: FIN + text opcode, masked."""
    payload_bytes = payload.encode("utf-8")
    length = len(payload_bytes)
    mask_key = bytes(random.getrandbits(8) for _ in range(4))
    masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload_bytes))
    if length < 126:
        header = bytes([0x81, 0x80 | length])
    elif length < 65536:
        header = bytes([0x81, 0x80 | 126]) + struct.pack("!H", length)
    else:
        header = bytes([0x81, 0x80 | 127]) + struct.pack("!Q", length)
    return header + mask_key + masked


def build_handshake_request(path_and_query: str, extra_headers: dict) -> bytes:
    key = make_sec_websocket_key()
    lines = [
        f"GET {path_and_query} HTTP/1.1",
        f"Host: {HOST}:{PORT}",
        "Upgrade: websocket",
        "Connection: Upgrade",
        f"Sec-WebSocket-Key: {key}",
        "Sec-WebSocket-Version: 13",
    ]
    for k, v in extra_headers.items():
        lines.append(f"{k}: {v}")
    lines.append("")
    lines.append("")
    return "\r\n".join(lines).encode("ascii")


# ---------------------------------------------------------------------------
# Log helpers (same approach as graphbridge_handshake_security_test.py)
# ---------------------------------------------------------------------------

def find_log_path(explicit):
    if explicit:
        return explicit
    this_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", ".."))
    uprojects = glob.glob(os.path.join(project_root, "*.uproject"))
    if uprojects:
        project_name = os.path.splitext(os.path.basename(uprojects[0]))[0]
        exact = os.path.join(project_root, "Saved", "Logs", f"{project_name}.log")
        if os.path.isfile(exact):
            return exact
    candidates = glob.glob(os.path.join(project_root, "Saved", "Logs", "*.log"))
    bare = [c for c in candidates if "-backup-" not in os.path.basename(c)
            and not os.path.basename(c).rsplit(".", 1)[0].split("_")[-1].isdigit()]
    pool = bare or candidates
    if not pool:
        return None
    return max(pool, key=os.path.getmtime)


def read_session_token(project_root: str) -> str:
    token_path = os.path.join(project_root, "Saved", "GraphBridge", "session_token.txt")
    with open(token_path, "r", encoding="utf-8") as f:
        return f.read().strip()


class LogWindow:
    """Captures a slice of the log file between two points in time, and
    counts marker occurrences within it -- so concurrent test runs don't
    see each other's markers."""

    def __init__(self, log_path: str):
        self.log_path = log_path
        self.start_offset = self._size()

    def _size(self) -> int:
        try:
            return os.path.getsize(self.log_path)
        except OSError:
            return 0

    def read_new(self, settle_seconds: float = 2.0) -> str:
        """Wait for the log to settle, then return everything written since
        this window was opened."""
        time.sleep(settle_seconds)
        try:
            with open(self.log_path, "r", encoding="utf-8", errors="replace") as f:
                f.seek(self.start_offset)
                return f.read()
        except OSError:
            return ""

    def count_marker(self, marker: str, settle_seconds: float = 2.0) -> int:
        return self.read_new(settle_seconds).count(marker)


# ---------------------------------------------------------------------------
# Connection helpers
# ---------------------------------------------------------------------------

def open_connection(token: str | None, extra_headers: dict | None = None) -> socket.socket:
    """Opens a raw TCP socket and sends the WS handshake. token=None omits the
    query param entirely (absent token); pass a wrong string to simulate an
    invalid one."""
    path = f"/?token={token}" if token is not None else "/"
    handshake = build_handshake_request(path, extra_headers or {})
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect((HOST, PORT))
    sock.sendall(handshake)
    return sock


def drain_handshake_response(sock: socket.socket) -> bytes:
    """Reads until the HTTP header terminator (or timeout/close)."""
    buf = b""
    try:
        while b"\r\n\r\n" not in buf:
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    return buf


# ---------------------------------------------------------------------------
# Test 1: concurrent mixed auth
# ---------------------------------------------------------------------------

def run_authenticated_client(token: str, client_idx: int, duration_s: float,
                              sent_markers: list, stop_event: threading.Event):
    try:
        sock = open_connection(token)
        drain_handshake_response(sock)
        sock.settimeout(0.5)
        end = time.time() + duration_s
        i = 0
        while time.time() < end and not stop_event.is_set():
            marker = f"__CONC_AUTH_OK_{client_idx}_{i}_{uuid.uuid4().hex[:8]}__"
            sock.sendall(build_ws_text_frame(f"LIST_VARIABLES|/Game/_Spike/DoesNotNeedToExist|{marker}"))
            sent_markers.append(marker)
            i += 1
            try:
                sock.recv(4096)  # drain any response, best-effort
            except socket.timeout:
                pass
            time.sleep(random.uniform(0.01, 0.05))
        sock.close()
    except OSError:
        pass


def run_unauthenticated_client(bad_mode: str, client_idx: int, duration_s: float,
                                sent_markers: list, stop_event: threading.Event):
    """bad_mode: 'wrong' (wrong token) or 'absent' (no token param at all)."""
    try:
        token = "0" * 32 if bad_mode == "wrong" else None
        sock = open_connection(token)
        end = time.time() + duration_s
        i = 0
        while time.time() < end and not stop_event.is_set():
            marker = f"__CONC_AUTH_BAD_{bad_mode}_{client_idx}_{i}_{uuid.uuid4().hex[:8]}__"
            try:
                sock.sendall(build_ws_text_frame(f"LIST_VARIABLES|/Game/_Spike/DoesNotNeedToExist|{marker}"))
            except OSError:
                break  # server already closed us, as expected
            sent_markers.append(marker)
            i += 1
            time.sleep(random.uniform(0.01, 0.05))
        sock.close()
    except OSError:
        pass


def test_concurrent_mixed_auth(log_path: str, token: str, duration_s: float = 10.0) -> bool:
    print(f"\n=== TEST 1: concurrent_mixed_auth ({duration_s:.0f}s, 16 valid / 16 bad) ===")
    window = LogWindow(log_path)
    stop_event = threading.Event()

    ok_markers: list = []
    bad_markers: list = []
    threads = []

    for i in range(16):
        t = threading.Thread(target=run_authenticated_client,
                              args=(token, i, duration_s, ok_markers, stop_event))
        threads.append(t)
    for i in range(8):
        t = threading.Thread(target=run_unauthenticated_client,
                              args=("wrong", i, duration_s, bad_markers, stop_event))
        threads.append(t)
    for i in range(8):
        t = threading.Thread(target=run_unauthenticated_client,
                              args=("absent", i, duration_s, bad_markers, stop_event))
        threads.append(t)

    random.shuffle(threads)
    for t in threads:
        t.start()
        time.sleep(random.uniform(0, 0.01))  # jitter connect timing
    for t in threads:
        t.join(timeout=duration_s + 10)

    log_text = window.read_new(settle_seconds=3.0)

    ok_executed = sum(1 for m in ok_markers if m in log_text)
    bad_executed = sum(1 for m in bad_markers if m in log_text)

    print(f"  authenticated commands sent: {len(ok_markers)}, executed (found in log): {ok_executed}")
    print(f"  unauthenticated commands attempted: {len(bad_markers)}, executed (found in log): {bad_executed}")

    passed = (bad_executed == 0) and (ok_executed == len(ok_markers)) and len(ok_markers) > 0
    print(f"  RESULT: {'PASS' if passed else 'FAIL'}")
    return passed


# ---------------------------------------------------------------------------
# Test 2: churn / address reuse
# ---------------------------------------------------------------------------

def churn_authenticated(token: str, iterations: int, sent_markers: list, stop_event: threading.Event,
                         lock: threading.Lock, worker_idx: int = 0):
    for i in range(iterations):
        if stop_event.is_set():
            break
        try:
            sock = open_connection(token)
            # Still drain the handshake response -- without this, the socket
            # can be torn down (see the SHUT_WR below) before the client-side
            # TCP stack has even confirmed the server accepted the upgrade,
            # which silently drops the command instead of exercising
            # anything server-side (confirmed: an earlier version of this
            # function that skipped this got 0 server-side executions out of
            # hundreds of "successful" sendall() calls -- a test bug, not a
            # finding). No recv-WAIT for the *command's* response, though:
            # that's the part we want gone, to free the connection for reuse
            # at the highest rate this client can drive.
            drain_handshake_response(sock)
            marker = f"__CHURN_OK_{worker_idx}_{i}_{uuid.uuid4().hex[:8]}__"
            sock.sendall(build_ws_text_frame(f"LIST_VARIABLES|/Game/_Spike/DoesNotNeedToExist|{marker}"))
            with lock:
                sent_markers.append(marker)
            # Graceful half-close so the OS flushes the already-buffered send
            # before the fd is torn down, instead of risking a discard.
            try:
                sock.shutdown(socket.SHUT_WR)
            except OSError:
                pass
            sock.close()
        except OSError:
            pass


def churn_unauthenticated_probe(stop_event: threading.Event, sent_markers: list, lock: threading.Lock,
                                 worker_idx: int = 0):
    i = 0
    while not stop_event.is_set():
        try:
            marker = f"__CHURN_BAD_{worker_idx}_{i}_{uuid.uuid4().hex[:8]}__"
            handshake = build_handshake_request("/", {})
            frame = build_ws_text_frame(f"LIST_VARIABLES|/Game/_Spike/DoesNotNeedToExist|{marker}")
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2)
            sock.connect((HOST, PORT))
            # Same burst: handshake (with NO token) immediately followed by
            # the command frame, no read in between -- maximizes the chance
            # of landing in the exact window this fix closes. No sleep: fire
            # as fast as this thread can manage a TCP connect.
            sock.sendall(handshake + frame)
            with lock:
                sent_markers.append(marker)
            i += 1
            sock.close()
        except OSError:
            pass


def test_churn_address_reuse(log_path: str, token: str, iterations: int = 1000,
                              churn_workers: int = 8, probe_workers: int = 8) -> bool:
    print(f"\n=== TEST 2: churn_address_reuse ({churn_workers}x{iterations} authenticated churn cycles, "
          f"{probe_workers} concurrent unauthenticated probers, no wait between cycles) ===")
    window = LogWindow(log_path)
    stop_event = threading.Event()
    list_lock = threading.Lock()

    ok_markers: list = []
    bad_markers: list = []

    churn_threads = [
        threading.Thread(target=churn_authenticated, args=(token, iterations, ok_markers, stop_event, list_lock, w))
        for w in range(churn_workers)
    ]
    probe_threads = [
        threading.Thread(target=churn_unauthenticated_probe, args=(stop_event, bad_markers, list_lock, w))
        for w in range(probe_workers)
    ]

    start = time.time()
    for t in probe_threads:
        t.start()
    for t in churn_threads:
        t.start()
    for t in churn_threads:
        t.join(timeout=300)
    stop_event.set()
    for t in probe_threads:
        t.join(timeout=10)
    elapsed = time.time() - start

    log_text = window.read_new(settle_seconds=3.0)

    ok_executed = sum(1 for m in ok_markers if m in log_text)
    bad_executed = sum(1 for m in bad_markers if m in log_text)

    print(f"  elapsed: {elapsed:.1f}s")
    print(f"  authenticated churn commands sent: {len(ok_markers)}, executed: {ok_executed}")
    print(f"  unauthenticated probe attempts: {len(bad_markers)}, executed: {bad_executed}")

    passed = (bad_executed == 0) and len(ok_markers) > 0
    print(f"  RESULT: {'PASS' if passed else 'FAIL'}")
    return passed


# ---------------------------------------------------------------------------
# Test 3: disconnect before reply
# ---------------------------------------------------------------------------

def test_disconnect_before_reply(log_path: str, token: str, iterations: int = 200) -> bool:
    print(f"\n=== TEST 3: disconnect_before_reply ({iterations} iterations) ===")
    window = LogWindow(log_path)
    sent_markers = []

    for i in range(iterations):
        try:
            sock = open_connection(token)
            drain_handshake_response(sock)
            # LIST_ASSETS with no filter walks the whole asset registry --
            # measurable game-thread cost (confirmed: multi-KB response,
            # hundreds of entries, in this project) -- unlike a trivial
            # single-Blueprint lookup, this is likely to still be running on
            # the game thread's AsyncTask when we yank the socket.
            marker = f"__DISCONNECT_{i}_{uuid.uuid4().hex[:8]}__"
            sock.sendall(build_ws_text_frame(f"LIST_ASSETS|{marker}"))
            sent_markers.append(marker)
            sock.close()  # immediately -- no read, no graceful close handshake
        except OSError:
            pass

    log_text = window.read_new(settle_seconds=5.0)
    executed = sum(1 for m in sent_markers if m in log_text)

    # "No crash" is checked by the caller (server still reachable after this
    # test runs). "No ensure" -- scan the new log slice for UE's ensure-failure
    # marker.
    ensure_failures = len(re.findall(r"Ensure condition failed", log_text))

    print(f"  commands sent then immediately disconnected: {len(sent_markers)}")
    print(f"  executed anyway (server-side, expected -- server had already accepted the frame): {executed}")
    print(f"  ensure failures found in log: {ensure_failures}")

    passed = ensure_failures == 0
    print(f"  RESULT: {'PASS' if passed else 'FAIL'}")
    return passed


# ---------------------------------------------------------------------------
# Test 4: buffered pre-auth frame (folded in from graphbridge_handshake_security_test.py)
# ---------------------------------------------------------------------------

def test_buffered_pre_auth_frame(log_path: str, token: str) -> bool:
    print("\n=== TEST 4: buffered_pre_auth_frame (folded in from graphbridge_handshake_security_test.py) ===")
    window = LogWindow(log_path)
    results = {}

    # Case 1: wrong token, command in same burst.
    marker1 = f"__PREAUTH_WRONGTOKEN_{uuid.uuid4().hex}__"
    wrong_token = "0" * len(token)
    handshake1 = build_handshake_request(f"/?token={wrong_token}", {})
    frame1 = build_ws_text_frame(f"LIST_ASSETS|{marker1}")
    sock1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock1.settimeout(5)
    sock1.connect((HOST, PORT))
    sock1.sendall(handshake1 + frame1)
    try:
        sock1.recv(4096)
    except socket.timeout:
        pass
    sock1.close()

    # Case 2: valid token, non-loopback Origin, command in same burst.
    marker2 = f"__PREAUTH_BADORIGIN_{uuid.uuid4().hex}__"
    handshake2 = build_handshake_request(f"/?token={token}", {"Origin": "http://evil.example.com"})
    frame2 = build_ws_text_frame(f"LIST_ASSETS|{marker2}")
    sock2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock2.settimeout(5)
    sock2.connect((HOST, PORT))
    sock2.sendall(handshake2 + frame2)
    try:
        sock2.recv(4096)
    except socket.timeout:
        pass
    sock2.close()

    log_text = window.read_new(settle_seconds=2.0)
    found1 = marker1 in log_text
    found2 = marker2 in log_text
    print(f"  wrong-token burst executed: {found1} (expect False)")
    print(f"  bad-origin burst executed: {found2} (expect False)")

    results["wrong_token"] = not found1
    results["bad_origin"] = not found2
    passed = all(results.values())
    print(f"  RESULT: {'PASS' if passed else 'FAIL'}")
    return passed


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def server_is_alive() -> bool:
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(3)
        sock.connect((HOST, PORT))
        sock.close()
        return True
    except OSError:
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", default=None)
    parser.add_argument("--mixed-duration", type=float, default=10.0)
    parser.add_argument("--churn-iterations", type=int, default=1000)
    parser.add_argument("--disconnect-iterations", type=int, default=200)
    parser.add_argument("--tests", default="1,2,3,4", help="comma-separated test numbers to run")
    args = parser.parse_args()

    this_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", ".."))

    log_path = find_log_path(args.log)
    if not log_path:
        print("ERROR: could not auto-detect the editor log. Pass --log explicitly.")
        sys.exit(1)
    print(f"Watching log: {log_path}")

    try:
        token = read_session_token(project_root)
    except OSError:
        print("ERROR: could not read session token. Start the GraphBridge server first.")
        sys.exit(1)
    print(f"Valid token: {token}")

    if not server_is_alive():
        print("ERROR: server not reachable at startup.")
        sys.exit(1)

    which = set(args.tests.split(","))
    results = {}

    if "1" in which:
        results["concurrent_mixed_auth"] = test_concurrent_mixed_auth(log_path, token, args.mixed_duration)
        if not server_is_alive():
            print("FATAL: server unreachable after test 1 -- crash suspected.")
            sys.exit(2)

    if "2" in which:
        results["churn_address_reuse"] = test_churn_address_reuse(log_path, token, args.churn_iterations)
        if not server_is_alive():
            print("FATAL: server unreachable after test 2 -- crash suspected.")
            sys.exit(2)

    if "3" in which:
        results["disconnect_before_reply"] = test_disconnect_before_reply(log_path, token, args.disconnect_iterations)
        if not server_is_alive():
            print("FATAL: server unreachable after test 3 -- crash suspected.")
            sys.exit(2)

    if "4" in which:
        results["buffered_pre_auth_frame"] = test_buffered_pre_auth_frame(log_path, token)
        if not server_is_alive():
            print("FATAL: server unreachable after test 4 -- crash suspected.")
            sys.exit(2)

    print("\n=== SUMMARY ===")
    for name, passed in results.items():
        print(f"  {name}: {'PASS' if passed else 'FAIL'}")
    print(f"  server alive at end: {server_is_alive()}")

    if not all(results.values()):
        sys.exit(1)


if __name__ == "__main__":
    main()
