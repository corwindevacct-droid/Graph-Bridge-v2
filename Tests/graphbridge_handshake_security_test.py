# GraphBridge AI - graphbridge_handshake_security_test.py
# Copyright 2026 Corwin Hicks. All Rights Reserved.

"""
Adversarial test for the WebSocket handshake security checks added in
v2.0.1 (loopback bind, Origin rejection, per-session token).

This deliberately does NOT use the `websockets` library's high-level
connect(), because that call cannot return a usable connection object until
the client has already parsed a complete HTTP 101 response -- which hides
the exact race this test needs to exercise. The server's Open-branch
rejection (see GraphBridgeAutomationLibrary.cpp's setOnClientMessageCallback)
runs AFTER the 101 response has already been sent by IXWebSocket's
serverHandshake(), so a client always sees a successful-looking upgrade;
the only question is whether a command frame sent in the same TCP burst as
the handshake request reaches ExecuteAtomicCommand before the server-side
rejection closes the connection.

This script opens a raw TCP socket, hand-builds the HTTP upgrade request and
a masked WebSocket text frame carrying a uniquely-tagged command, and sends
both in a single sendall() call with no read in between -- the closest an
application-level test can get to "same burst, no waiting for a response".

Usage:
    python graphbridge_handshake_security_test.py [--log PATH_TO_EDITOR_LOG]

Requires: the GraphBridge server running on 127.0.0.1:8080 (Start Server in
the panel), and the editor's live Output Log (Saved/Logs/<Project>.log) --
pass --log explicitly if auto-detection picks the wrong file.
"""

import argparse
import base64
import glob
import os
import random
import socket
import struct
import sys
import time
import uuid

HOST = "127.0.0.1"
PORT = 8080


def make_sec_websocket_key() -> str:
    return base64.b64encode(bytes(random.getrandbits(8) for _ in range(16))).decode("ascii")


def build_ws_text_frame(payload: str) -> bytes:
    """Hand-rolled RFC 6455 client frame: FIN + text opcode, masked (client
    frames MUST be masked or a spec-compliant server will reject them as a
    protocol error, which would produce a false positive for this test)."""
    payload_bytes = payload.encode("utf-8")
    length = len(payload_bytes)
    if length > 125:
        raise ValueError("test payloads must stay under 126 bytes for this minimal frame builder")

    mask_key = bytes(random.getrandbits(8) for _ in range(4))
    masked = bytes(b ^ mask_key[i % 4] for i, b in enumerate(payload_bytes))

    header = bytes([0x81, 0x80 | length])  # FIN=1 opcode=text, MASK=1 + length
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


def run_adversarial_case(name: str, path_and_query: str, extra_headers: dict, command: str) -> bytes:
    """
    Opens a raw socket, sends the handshake request AND the command frame in
    one sendall() burst (no read in between), then reads back whatever the
    server sends within the timeout. Returns the raw bytes received (for the
    caller to inspect for a close frame / truncated response).
    """
    print(f"\n--- {name} ---")
    print(f"  request: GET {path_and_query}  headers={extra_headers}")
    print(f"  command frame payload: {command!r}")

    handshake = build_handshake_request(path_and_query, extra_headers)
    frame = build_ws_text_frame(command)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    try:
        sock.connect((HOST, PORT))
        # Single sendall() call: handshake request immediately followed by
        # the command frame, with no recv() in between -- this is the burst.
        sock.sendall(handshake + frame)

        chunks = []
        try:
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
        except socket.timeout:
            pass
        return b"".join(chunks)
    finally:
        sock.close()


def describe_response(raw: bytes) -> str:
    if not raw:
        return "(no bytes received -- connection closed immediately)"
    if b"\r\n\r\n" in raw:
        head, _, rest = raw.partition(b"\r\n\r\n")
        status_line = head.split(b"\r\n", 1)[0].decode("latin-1", errors="replace")
        info = f"HTTP response: {status_line}"
        if rest:
            # A close frame (opcode 0x8) starting right after the HTTP
            # headers means the server accepted the upgrade, then closed.
            if len(rest) >= 2 and (rest[0] & 0x0F) == 0x8:
                close_code = struct.unpack("!H", rest[2:4])[0] if len(rest) >= 4 else None
                reason = rest[4:].decode("utf-8", errors="replace") if len(rest) > 4 else ""
                info += f" ; then a CLOSE frame, code={close_code} reason={reason!r}"
            else:
                info += f" ; then {len(rest)} bytes of non-close data: {rest[:200]!r}"
        return info
    return f"raw bytes (no HTTP header terminator found): {raw[:200]!r}"


def find_log_path(explicit: str | None) -> str | None:
    if explicit:
        return explicit
    # Best-effort auto-detect: <ProjectRoot>/Saved/Logs/<ProjectName>.log,
    # where this file lives at <ProjectRoot>/Plugins/GraphBridgev2/Tests/.
    this_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", ".."))

    # The engine also writes non-editor-log files into Saved/Logs (e.g.
    # cef3.log for the Chromium-based UI, UnrealVersionSelector-*.log) that
    # can be newer than the real editor log -- naive "most recently
    # modified" picks those by accident. Anchor on the actual .uproject
    # name instead, which is what UE names its own log after.
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


def marker_in_log(log_path: str, marker: str) -> bool:
    try:
        with open(log_path, "r", encoding="utf-8", errors="replace") as f:
            return marker in f.read()
    except OSError as e:
        print(f"  (could not read log {log_path}: {e})")
        return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", default=None, help="Path to the editor's live log file")
    args = parser.parse_args()

    this_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(this_dir, "..", "..", ".."))
    token_path = os.path.join(project_root, "Saved", "GraphBridge", "session_token.txt")
    try:
        with open(token_path, "r", encoding="utf-8") as f:
            valid_token = f.read().strip()
    except OSError:
        print(f"ERROR: could not read session token from {token_path}")
        print("Start the GraphBridge server in the editor first.")
        sys.exit(1)

    log_path = find_log_path(args.log)
    if not log_path:
        print("ERROR: could not auto-detect the editor log. Pass --log explicitly.")
        sys.exit(1)
    print(f"Watching log: {log_path}")
    print(f"Valid token (for the Origin test): {valid_token}")

    results = {}

    # --- Case 1: wrong token, command sent in the same burst -------------
    marker1 = f"__SECURITY_TEST_WRONGTOKEN_{uuid.uuid4().hex}__"
    wrong_token = "0" * len(valid_token)
    raw1 = run_adversarial_case(
        "Case 1: wrong token, immediate command frame",
        f"/?token={wrong_token}",
        {},
        f"LIST_ASSETS|{marker1}",
    )
    print(f"  response: {describe_response(raw1)}")
    time.sleep(1.5)  # let the log flush
    found1 = marker_in_log(log_path, marker1)
    print(f"  marker found in log: {found1}")
    results["wrong_token"] = not found1

    # --- Case 2: valid token, non-loopback Origin, immediate command -----
    marker2 = f"__SECURITY_TEST_BADORIGIN_{uuid.uuid4().hex}__"
    raw2 = run_adversarial_case(
        "Case 2: valid token + non-loopback Origin, immediate command frame",
        f"/?token={valid_token}",
        {"Origin": "http://evil.example.com"},
        f"LIST_ASSETS|{marker2}",
    )
    print(f"  response: {describe_response(raw2)}")
    time.sleep(1.5)
    found2 = marker_in_log(log_path, marker2)
    print(f"  marker found in log: {found2}")
    results["bad_origin"] = not found2

    print("\n=== SUMMARY ===")
    for name, passed in results.items():
        print(f"  {name}: {'PASS (command never reached dispatch)' if passed else 'FAIL (command WAS dispatched -- add the authenticated flag)'}")

    if not all(results.values()):
        sys.exit(1)


if __name__ == "__main__":
    main()
