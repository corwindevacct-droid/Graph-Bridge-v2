# Changelog

## v2.0.1 (Security Hardening — Point Release)

**This is a security patch, not a feature release.** No command behavior
changed; no new tools were added. See README.md's new Security section for
the full threat model.

### Fixed
- WebSocket server now binds explicitly to `127.0.0.1` instead of relying on
  the underlying library's default host parameter.
- Connections are now rejected before any command can be dispatched if their
  `Origin` header is present and not a loopback origin (a non-browser client
  sending no `Origin` header at all is unaffected).
- Every WebSocket connection must now present a per-session auth token,
  minted fresh each server start and compared in constant time. The bundled
  Python client picks this up automatically; see README.md.
- `RUN_PYTHON` (arbitrary Python execution in the editor process) is now
  **off by default**, gated by a new Project Settings toggle enforced at the
  command-dispatch boundary. Turning it on logs a warning.
- The LLM provider API key no longer lives in project config
  (`DefaultEditorPerProjectUserSettings.ini`, a file inside your
  repository). On Windows it now lives in Credential Manager; on macOS (see
  Known Limitations below) and any other platform it falls back to a
  `GRAPHBRIDGE_API_KEY` environment variable.
- Fixed two distinct concurrency defects in the WebSocket handshake/dispatch
  path, found by code audit and confirmed with two different kinds of
  testing (see below — one was reproduced live, one was not, and this entry
  is deliberately specific about which is which):
  - **Authentication state was keyed by connection identity, not held per
    connection.** The old design tracked "authenticated" in a
    `TSet<ix::WebSocket*>` guarded by a critical section — sound against a
    torn read, but not against IXWebSocket's own allocation pattern: each
    `WebSocket` is a `make_shared` local, destroyed when that connection's
    dedicated thread returns, so its address is free for the allocator to
    hand to the very next connection. Nothing in that design ruled out a
    stale `TSet` entry outliving its connection and letting a new,
    never-authenticated connection inherit "authenticated" for free by
    landing at a recycled address. Authentication state is now a genuinely
    per-connection object (`FGraphBridgeConnectionState`, one instance per
    connection via IXWebSocket's `setConnectionStateFactory`/
    `setOnConnectionCallback`) — there is nothing left to key by address.
  - **Reply path use-after-free.** The game-thread dispatch task captured a
    raw `ix::WebSocket*`; if the client disconnected before the task ran,
    the connection's own worker thread had already destroyed the object by
    the time the game thread dereferenced it. The task now captures a
    `std::weak_ptr<ix::WebSocket>` and locks it on the game thread,
    discarding the command if the connection is gone, instead of trusting a
    pointer that could already be dangling.

  **What was actually demonstrated, and how:** `Tests/graphbridge_concurrency_test.py`
  adds real concurrent-load coverage (mixed valid/invalid auth under
  contention, connect/command/disconnect churn racing an unauthenticated
  prober, disconnect-before-reply cycles), measured server-side by log
  marker rather than by client response, run against both the pre-fix and
  post-fix builds at escalating scale (up to 128 concurrent clients; up to
  16 churn workers against 16 probing workers, 21,725 unauthenticated probe
  attempts in one run). At every scale tried, both builds showed zero
  unauthenticated executions — this network-level testing did not reproduce
  the `TSet` design's address-reuse identity flaw as a live bypass. That
  defect is real and provable by reading the code (nothing in the old
  design ruled it out), but this suite should be read as regression
  coverage for the per-connection mechanism going forward, not as proof the
  old mechanism was exploited or exploitable via this method.
  The use-after-free is a different story: rebuilding the pre-fix code
  under AddressSanitizer (MSVC `/fsanitize=address`) and running the
  disconnect-before-reply case reproduced a heap-use-after-free
  deterministically, on the first cycle. ASan's own report shows the
  `WebSocket` allocated on the connection-accepting thread, freed on that
  connection's own dedicated worker thread, and then read on the game
  thread inside `ExecuteAtomicCommand`/`SendResponse` — exactly the
  raw-pointer path described above. The identical test against the
  post-fix (`weak_ptr`-based) build under the same instrumentation came
  back clean. This defect was directly demonstrated, not just inferred.
- Replaced a second, unrelated concurrency hazard found while auditing the
  above: dispatch results were captured via a single global `FString*`
  (`GSyncResultCapture`, already flagged in its own comment as
  "non-reentrant") set for the duration of a synchronous call and cleared
  after. A command that synchronously triggers another synchronous dispatch
  before the outer one finishes — e.g. a `RUN_PYTHON` script that itself
  issues a GraphBridge command — would have the inner call's cleanup null
  out the outer call's capture pointer, silently losing its result. Dispatch
  now threads an explicit `FGraphBridgeCommandContext` (with its own
  `ResultBuffer`) through every call instead of relying on shared global
  state, which has no such nesting hazard. This has no dedicated regression
  test in this release (the reentrancy scenario needs its own harness, not
  the concurrency suite above, which tests threads rather than nesting);
  treat it as fixed-by-audit like the address-reuse item above, not as
  independently verified under load.
- The command-received log line no longer writes full command bodies (e.g.
  `RUN_PYTHON` source, or its output when called as an LLM tool) to the
  Output Log at a level enabled by default — same disclosure class as the
  old ini-stored API key, since Output Logs get pasted into bug reports and
  Discord threads. Dropped to Verbose and truncated to a bounded prefix.
- The session token file (`Saved/GraphBridge/session_token.txt`) is now
  deleted when the server stops, including on editor exit — a stale token
  surviving past its server used to produce a confusing "wrong token"
  failure for the next session instead of a clear "no server running" one.

### Known Limitations
- **macOS Keychain storage is not enabled in this release.** The Keychain
  code path exists in `GraphBridgeCredentialStore.cpp` but ships compiled
  out (`GRAPHBRIDGE_ENABLE_MAC_KEYCHAIN=0`) because it has not been built or
  tested against a real Mac toolchain. macOS uses the `GRAPHBRIDGE_API_KEY`
  environment variable for now; Keychain support is planned once verified
  on-platform.

### Upgrading
- If you had a key saved in the old panel, it is migrated automatically on
  first load into OS credential storage and cleared from the ini. **Rotate
  that key with your provider (Anthropic/OpenAI) if this project's `Config/`
  folder was ever committed to version control** — it may still be present
  in your git history even after the plaintext value is cleared.
- If you rely on `RUN_PYTHON`, re-enable it in Project Settings → Plugins →
  GraphBridge AI after upgrading.
- No script changes required for the bundled Python tools — the session
  token is picked up automatically. A custom script talking to the bridge
  directly (raw `websockets.connect`, curl, etc.) needs the token appended
  as `?token=...` on the connection URI; see README.md's Security section.

### Breaking Changes
None to the command protocol. The only externally-visible behavior change
is `RUN_PYTHON` requiring an explicit opt-in, and any custom (non-bundled)
WebSocket client now needing the session token.

---

## v2.0.0 (Final Release)

**This is the final v2 release.** GraphBridgev2 is now feature-complete, stable, and locked for backward compatibility. Bug fixes and critical updates will be maintained in v2; new features will be delivered in v3 (a new plugin).

### Major Features
- **129 Production-Ready Tools**: Complete automation coverage spanning Blueprint wiring, animation, character setup, materials, widgets, level management, and more
- **Fully-Typed Parameters (Tier 1+2)**: 44 critical tools now include rich parameter types (enums, bounded floats/ints), semantic defaults, and detailed docstrings
- **Auto-Generated Python Toolset**: Manifest-driven generator ensures Python signatures always match C++ implementations
- **MCP Server Support**: Full MCP 0.1+ compatibility with dual-server coexistence (:8090 GraphBridge + Epic's :8000 without conflicts)
- **Deep Verticalization**: Specialized support for animation montages, IK rigs, retargeters, skeleton sockets, state machines, and character controllers
- **Comprehensive Test Harness**: Drift validation, coexistence verification, golden-file testing, and automation framework

### Breaking Changes
None. v2.0.0 maintains full backward compatibility with all previous releases.

### Known Limitations
- Tier 3 tools (6 remaining) have basic string parameter types; full enums and bounds will be added in v3
- Python 2.7 not supported; requires Python 3.9+

### Maintenance Policy
- **v2**: Bug fixes, critical updates, security patches
- **v3**: New features, major enhancements (new plugin, separate repository)
- **No breaking changes** to v2; safe for long-term production use

---

## v1.0.0 (Previous Release)
Initial release with 120 tools and basic Python support.
