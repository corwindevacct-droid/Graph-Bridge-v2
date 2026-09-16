# GraphBridgev2

**The most complete Blueprint automation plugin for Unreal Engine 5.8+ with AI-native tooling.**

GraphBridgev2 is a production-ready automation framework providing 129 fully-typed tools for Blueprint wiring, character setup, animation pipeline acceleration, and AI-assisted editor workflows. This is the **final v2 release** — feature-complete, stable, and maintained as-is. Future innovation will be delivered in v3 (a new plugin).

## Features

- **129 Production-Ready Tools**: Complete coverage of Blueprint operations, animation, character, materials, widgets, and level management
- **Fully Typed Parameters (Tier 1+2)**: 44 critical tools with enums, bounded floats/ints, defaults, and docstrings
- **Python Toolset**: Auto-generated from C++ manifest; runs against WebSocket (:8080) or MCP (:8090)
- **MCP 0.1+ Compatible**: Dual-server architecture—runs alongside Epic's native MCP without conflicts
- **Deep Verticalization**: Animation montages, IK rigs, skeleton sockets, state machines, character setup
- **Comprehensive Test Harness**: Drift tests, coexistence verification, golden-file validation
- **Local-Only Bridge**: loopback-only bind and Origin rejection on both WebSocket and MCP, plus a WebSocket-specific handshake-race token — see [Security](#security) below for what this does and doesn't protect against

## Competitive Positioning

**Better than Epic's native Bridge:**
- Richer parameter typing (not just strings)—bounds, enums, semantic validation
- Deep animation/IK/character support
- Proven at scale; fully tested

**Stable & Maintained:**
- v2.0.0 is feature-complete and locked
- Bug fixes and updates in v2; new features in v3
- No breaking changes

## Installation

### Fab Marketplace (Recommended)
Open UE 5.8+, Marketplace → Search GraphBridgev2 → Install

### Manual Clone
\\\ash
cd YourProject/Plugins
git clone https://github.com/YourOrg/GraphBridgev2.git
\\\

## Compatibility

- Unreal Engine 5.8.0+
- Python 3.9+
- MCP 0.1+

## Security

GraphBridge runs two servers inside the editor process that accept commands
and execute them against your live editor — a WebSocket server (`:8080`)
and an MCP server over HTTP (`:8090`). That is inherently powerful, and this
section is here so you know exactly what protects each of them and what
doesn't. The two surfaces are protected differently, on purpose — see below.

**Threat model: localhost is not a trust boundary.** Any other process
running as the same OS user as the editor — not just your own scripts — can
open a socket to either server, **and can read any file that process can
read, including the WebSocket session token described below.** Origin checks
stop a browser tab (WebSocket connections aren't subject to CORS, so a
malicious web page could otherwise open one; a same-origin-policy-respecting
fetch() to the MCP port is blocked the same way), but neither Origin checks
nor the token do anything against another local program running as you —
that program can read the token file itself. Treat live access to either
bridge as equivalent to arbitrary code execution in the editor process and
scope your machine's trust accordingly. This is the same posture Epic takes
for its own native Unreal MCP server in 5.8 — we are not claiming to be more
"secure" than that, only to be honest about it.

What's actually in place:
- **Loopback-only bind, both servers.** Both bind explicitly to `127.0.0.1`
  — neither is ever reachable from another machine on your network.
- **Origin rejection, both servers, different strictness.** The WebSocket
  server closes a connection whose `Origin` header is present and not a
  loopback origin. The MCP server is stricter: it rejects *any* request that
  carries an `Origin` header at all, present because real MCP clients
  (Claude Code, Cursor, curl, SDKs) never send one — a browser's fetch()
  always does, on every request, so this fully blocks browser-originated
  MCP calls, not just non-loopback ones. Non-browser clients (the bundled
  Python tools, curl, a future CLI) send no `Origin` header at all on either
  server, which is normal and always passes.
- **Per-session token — WebSocket only, not MCP.** The WebSocket server
  mints a random token every time it starts and requires it on every
  connection (compared in constant time). This exists specifically to close
  a race in the WebSocket handshake: a client that sends a command frame in
  the same TCP burst as its (ultimately-rejected) upgrade request could
  otherwise reach dispatch before the rejection closed the connection. **Do
  not read more into it than that** — per the threat model above, another
  local process as the same OS user can simply read
  `<Project>/Saved/GraphBridge/session_token.txt`, so the token is not a
  secret that stops same-user access; it stops the handshake race and,
  incidentally, a browser tab that guesses or brute-forces it. The MCP
  server has no equivalent token: its request/response model has no
  handshake-then-frames race for a token to close, so loopback-only bind
  plus the stricter Origin rejection above is its actual and complete bar.
  If you're scripting against WebSocket, you'll find the token two places:
  the GraphBridge AI panel (Window menu), and
  `<Project>/Saved/GraphBridge/session_token.txt` — `Saved/` is gitignored
  by every standard UE project template, so it never lands in source
  control. The bundled Python client (`graphbridge_bridge.py` and every
  `graphbridge_*.py` script built on it) reads this file automatically; you
  only need it yourself for manual/curl-style debugging.
- **`RUN_PYTHON` is off by default.** This command executes arbitrary Python
  inside the editor process — enabling it (Project Settings → Plugins →
  GraphBridge AI) is equivalent to granting arbitrary code execution to
  anything that can reach the bridge. It's enforced at the command-dispatch
  boundary, not just hidden from the panel, so it's off for every entry
  point (WebSocket, MCP, the in-editor chat panel) until you explicitly turn
  it on. Turning it on logs a warning in the Output Log every time.
- **API keys live in OS credential storage**, not project config — Windows
  Credential Manager, falling back to the `GRAPHBRIDGE_API_KEY` environment
  variable if no platform store is available. **On macOS as of v2.0.1, the
  Keychain path is not yet enabled** — it ships compiled out
  (`GRAPHBRIDGE_ENABLE_MAC_KEYCHAIN=0` in `GraphBridgev2.Build.cs`) because
  it hasn't been built or tested on a real Mac toolchain, so Mac always uses
  the `GRAPHBRIDGE_API_KEY` environment variable for now; Keychain support
  is planned for a follow-up release once verified. If you upgraded from an
  older version that stored the key in
  `DefaultEditorPerProjectUserSettings.ini`, the plugin migrates it
  automatically on first load and shows a one-time warning — **if that
  project's `Config/` folder was ever committed to version control, rotate
  that key**, since it may still be present in your git history even after
  the ini value is cleared.

What this does **not** protect against: another program on your machine
running as you (this is the localhost-is-not-a-trust-boundary point above),
a compromised editor process, or a Python dependency (`websockets`,
`anthropic`, `openai`) with a supply-chain issue. If you need stronger
isolation, don't run the bridge on a machine you don't otherwise trust.

## Documentation

- **INSTALLATION.md** — Setup steps
- **USAGE.md** — WebSocket and MCP endpoints
- **API.md** — All 129 tools documented
- **TROUBLESHOOTING.md** — Common issues
- **ROADMAP.md** — v2 complete; v3 vision

## Examples

See Examples/ folder for Python agent scripts.

## License

See LICENSE.md

---

v2.0.1 is the current shipping version — a security-hardening point
release on top of v2.0.0, with no command behavior changes. It receives
bug fixes and security patches; new features are planned for v3. See
CHANGELOG.md and RELEASE_HISTORY.md for the full version history.
