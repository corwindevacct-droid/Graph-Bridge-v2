# Packaging Procedure (Fab Submission)

This is the verified, working procedure for producing a Fab-compliant
submission zip. It exists because the two obvious methods each fail in a
different way — re-discovering that by trial and error is exactly what
this document is for.

## Why the obvious methods don't work

- **A plain repo zip / `git archive`** puts `Tests/graphbridge_concurrency_test.py`
  and `Tests/graphbridge_handshake_security_test.py` at the package root.
  Fab requires plugin Python live *only* under `Content/Python` — a raw
  repo zip violates that on its own, with no warning.
- **`UAT BuildPlugin` alone** respects `Config/FilterPlugin.ini` correctly
  (so `Tests/`, `Tools/`, and internal notes are excluded), but it also
  *adds* `Binaries/` and `Intermediate/` as its own build output — neither
  belongs in Fab's required Code Plugin structure (`.uplugin`, `Source/`,
  `Content/`, `Config/`, `Resources/`).

The working method combines both: build with `BuildPlugin` (so
`FilterPlugin.ini`'s exclusions apply), then manually strip the two
folders `BuildPlugin` adds on top.

## Procedure

1. **Build the plugin as it would ship**, into a scratch output directory:
   ```
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
     -Plugin="<path to>\GraphBridgev2.uplugin" ^
     -Package="<scratch output dir>\GraphBridgev2" ^
     -Rocket
   ```
   `-Rocket` is required against an installed (non-source) engine build.
   This step compiles the plugin fresh and copies `Config/`, `Content/`,
   `Resources/`, `Source/`, `README.md`, and everything else listed in
   `Config/FilterPlugin.ini`'s allow-list into the output directory —
   correctly excluding anything not on that list.

2. **Strip the build artifacts `BuildPlugin` added**:
   ```
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Binaries"
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Intermediate"
   ```
   These exist because `BuildPlugin` just compiled the module — they are
   not part of Fab's required structure and aren't needed for a customer
   to compile the plugin themselves.

3. **Zip the result.** The directory `<scratch output dir>\GraphBridgev2`
   is now the actual submission content — zip it directly (no further
   restructuring needed) for the Project File Link upload.

## Verification checklist (run every time, not just once)

- [ ] No `.py` file exists anywhere outside `Content/Python`:
      `find <output>/GraphBridgev2 -iname "*.py" | grep -v "Content/Python"`
      should print nothing.
- [ ] `Source/ThirdParty/ixwebsocket/LICENSE.txt` is present (IXWebSocket
      is BSD-3-Clause, vendored as source — it needs its own license file
      regardless of this plugin's own license).
- [ ] No `_deferred/`, `Tests/`, `Tools/`, or internal status notes
      (`FAB_SUBMISSION_README.md`, `Tests/TASK_*.md`, `HARNESS_STATUS.md`,
      etc.) anywhere in the output.
- [ ] `docs/` and `LICENSE.md` are present at the root (both are in
      `FilterPlugin.ini`'s allow-list; confirm they still are if that file
      changes).
- [ ] `Binaries/` and `Intermediate/` are both absent (step 2 above).
- [ ] `RunUAT BuildPlugin` itself reported `BUILD SUCCESSFUL` with no
      `EnhancedInput` (or other) module-dependency warnings — a compile
      warning here means something is wrong with `.uplugin`'s `Plugins`
      array, not just packaging.

## `Config/FilterPlugin.ini`'s current allow-list

```ini
[FilterPlugin]
/Config/
/Content/
/Resources/
/Source/
/README.md
/CHANGELOG.md
/docs/
/LICENSE.md
!/Content/Python/__pycache__/
!*.pdb
!*.obj
!*.sarif
```

If a new top-level file or folder needs to ship (another doc, an
`Examples/` folder once its content is fixed and ready, etc.), it must be
added here explicitly — `BuildPlugin` does not include anything by
default beyond the required `Source/`/`Content/`/`Config/`/`Resources/`.

## Tool count: 129 — conditional, not permanent

The router (`ExecuteAtomicCommand`/`ExecuteAtomicCommandExtended`), the
manifest (`FGraphBridgeToolManifest::BuildManifest()`), the MCP server,
and the LLM client all currently agree at exactly **129** commands —
verified by diffing the extracted command-name lists directly, not by
comparing counts that happen to match.

This holds **only because** MCP and LLM generate their tool lists from
the manifest at runtime (`FGraphBridgeToolManifest::GetManifest()`/
`FindTool()`) rather than maintaining their own hardcoded lists — that's
what commit `69445c9` ("refactor: migrate MCPServer and LLMClient to tool
manifest") changed, and it's what closed an earlier, real drift between
surfaces (measured at the time as 120 / 84 / 63 across router / manifest /
panel-adjacent counts before the migration).

**If any surface goes back to a hardcoded tool list — even a partial one,
even temporarily — this number can silently drift again**, and "129" in
the Fab listing becomes a false claim rather than a verified one. Re-run
the diff (not just a count comparison) before republishing that number if
any of `GraphBridgeMCPServer.cpp`, `GraphBridgeLLMClient.cpp`, or
`GraphBridgeToolManifest.cpp` changes.
