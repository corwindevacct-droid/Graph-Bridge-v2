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
`FilterPlugin.ini`'s top-level allow-list applies), then manually strip
everything `BuildPlugin` adds or leaks on top that the allow-list can't
keep out.

**Also use a short output path.** `-Package` under a deep temp directory
(e.g. a session scratchpad several folders deep) will make UBT's own
generated `.obj`/`.rsp`/`.sarif` paths exceed Windows's 260-character
limit, and `UnrealBuildTool.ActionGraph.CheckPathLengths` fails the build
outright (`Result: Failed (OtherCompilationError)`, exit code 6) — not a
warning, a hard stop. Package to something short and flat, e.g.
`C:\<something>\package_output\GraphBridgev2`.

## Procedure

1. **Build the plugin as it would ship**, into a short-path scratch output
   directory:
   ```
   "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin ^
     -Plugin="<path to>\GraphBridgev2.uplugin" ^
     -Package="<short scratch output dir>\GraphBridgev2" ^
     -Rocket
   ```
   `-Rocket` is required against an installed (non-source) engine build.
   This step compiles the plugin fresh and copies `Config/`, `Content/`,
   `Resources/`, `Source/`, `README.md`, and everything else listed in
   `Config/FilterPlugin.ini`'s top-level allow-list into the output
   directory.

2. **Manually strip everything that doesn't belong**, because two
   different things put unwanted content in the output and neither is
   fixed by editing `FilterPlugin.ini`:
   ```
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Binaries"
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Intermediate"
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Docs\fab"
   rmdir /s /q "<scratch output dir>\GraphBridgev2\Content\Python\__pycache__"
   ```
   - `Binaries/` and `Intermediate/` exist because `BuildPlugin` just
     compiled the module — not part of Fab's required structure and not
     needed for a customer to compile the plugin themselves.
   - `Docs/fab/` and `Content/Python/__pycache__/` are things this file
     used to claim `!`-prefixed `FilterPlugin.ini` rules would exclude.
     **They don't, in UE 5.8.** See "The `!` exclude rules below don't
     work" — do not re-add a subfolder exclude to `FilterPlugin.ini` and
     assume it's handled; strip it here instead.
   - **Also check `Source/` and `Content/` for untracked local files**
     before stripping — `BuildPlugin` copies whatever is physically on
     disk under an allow-listed folder, not just what's committed to git.
     A local-only test scaffold (e.g. `Source/GraphBridgev2Tests/`, kept
     untracked on purpose) will ship in the zip unless it's also removed
     here. Run `git status --short` in the plugin directory first and
     strip anything untracked that lives inside `Source/` or `Content/`
     and isn't meant to ship.

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
      etc.) anywhere in the output — including untracked local files under
      `Source/`/`Content/` (see step 2's last bullet).
- [ ] `Docs/fab/` and `Content/Python/__pycache__/` are both absent (the
      `!` rules for these in `FilterPlugin.ini` do not actually take
      effect — see below; strip them manually, step 2).
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
!/docs/fab/
!*.pdb
!*.obj
!*.sarif
```

If a new top-level file or folder needs to ship (another doc, an
`Examples/` folder once its content is fixed and ready, etc.), it must be
added here explicitly — `BuildPlugin` does not include anything by
default beyond the required `Source/`/`Content/`/`Config/`/`Resources/`.

### The `!` exclude rules below don't work

`!/Content/Python/__pycache__/` and `!/docs/fab/` are left in this file to
record intent, but verified against the UE 5.8 `RunUAT BuildPlugin` used
above, **neither one actually excludes anything.** Both subfolders still
land in the packaged output.

Root cause, traced through
`Engine/Source/Programs/Shared/EpicGames.Core/FileFilter.cs`
(`BuildPluginCommand.Automation.cs`'s `FilterPluginFiles` is what reads
this ini): the matcher is a last-matching-rule-wins tree over path
segments, and by that algorithm a later, more specific exclude *should*
beat an earlier, broader include of its parent directory. It does not
change the result in practice for either of these two rules in this
engine version — a more specific defect than "add `!` and forget it," and
not worth re-deriving each release. Do not add a new nested `!` exclude
under `/Content/`, `/Source/`, or `/docs/` and trust it; verify it against
a real build's output before relying on it, and strip manually
(step 2) if it doesn't take effect.

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
