# Release History

Factual record of every tagged release, populated from the repository's
tags and GitHub release metadata — not from memory or from CHANGELOG.md's
own framing. All 9 releases listed here have been marked as pre-releases
on GitHub with a supersession notice prepended to their description; none
were deleted or modified beyond that.

| Version | Engine | Date | Branch/ref | Notes |
|---|---|---|---|---|
| v1.0.0 | UE 5.7 (zip filename; release notes say "5.5 or later") | 2026-06-25 | `archive/main-pre-5.8` | Initial release. Predates the security release. |
| v1.0.1 | UE 5.7 | 2026-06-27 | `archive/main-pre-5.8` | Fab compliance fixes (PlatformAllowList, copyright headers, IXWebSocket relocation). Predates the security release. |
| v1.0.2 | UE 5.7 | 2026-06-27 | `archive/main-pre-5.8` | Packaging cleanup (Intermediate/, Live Coding artifacts, PDBs). Predates the security release. |
| v1.0.3 | UE 5.7 | 2026-06-28 | `archive/main-pre-5.8` | Fab review fixes (Binaries/ removal, third-party declaration, `CanContainContent: false`). Predates the security release. |
| v1.0.4 | UE 5.5 **and** UE 5.7 (two separate zips) | 2026-06-29 | `archive/main-pre-5.8` | Bug fix release (SPAWN_EVENT_NODE). Predates the security release. |
| v1.0.5 | UE 5.7 | 2026-07-01 | `archive/main-pre-5.8` | `EngineVersion` bumped to 5.7.0 in `.uplugin`; Fab listing metadata fixes. Predates the security release. |
| v1.0.9 | UE 5.7 | 2026-07-04 | `archive/main-pre-5.8` | Added MCP transport (81 commands). Predates the security release. |
| v1.0.10 | UE 5.7 | 2026-07-04 | `archive/main-pre-5.8` | MCP auto-start fix. Predates the security release. |
| v1.1.0 | UE 5.7 | 2026-07-05 | `archive/main-pre-5.8` | Blueprint completeness (enums, structs, function libraries), crash fix, multi-model agent support. Predates the security release. |
| v1.2.0 | UE 5.7 | 2026-07-06 | `archive/main-pre-5.8` | Animation state machines, Niagara authoring, character pipeline (physics assets, IK Rig/Retargeter). Predates the security release. |
| **v2.0.1** | **UE 5.8** | current | `master` | **Current shipping version.** Security hardening point release (per-connection auth state, reply-path use-after-free fix, credential storage moved out of plaintext config, RUN_PYTHON off by default). No GitHub Release has been cut for this version yet — see note below. |

All ten prior tags predate the v2.0.1 security release and should not be
used for new installs — each release's description on GitHub now says so
directly.

## Note: no release exists yet for `master`'s lineage

Every one of the 9 existing GitHub Releases points to a commit reachable
only from `archive/main-pre-5.8` — verified directly (`git merge-base
--is-ancestor` against both branch tips), not assumed. Nothing on
`master` — not v1.4.1 through v1.5.2, not v2.0.0, not v2.0.1 — has ever
had a GitHub Release cut for it. Publishing one for v2.0.1 is a real gap,
but it's a listing decision, not something done as part of this archival
pass.

## The branch split

`archive/main-pre-5.8` (formerly `main`, GitHub's default branch until
today) and `master` (now the default) are **unrelated git histories** —
confirmed by `git merge-base` returning nothing between them, not a
divergence from a shared commit. Every one of the 9 releases above was
built from the `archive/main-pre-5.8` lineage. `master` is a separate
history that reached feature parity and beyond: a full tree comparison
found only one file unique to `main` (`UE5.8_MIGRATION_NOTES.md`, now at
[`Archive/UE5.8_MIGRATION_NOTES.md`](Archive/UE5.8_MIGRATION_NOTES.md));
every feature attributed to `main` — Niagara, MCP, IK Rig, widgets,
physics assets, multi-graph spawning — exists on `master` at equal or
greater scope, and `master`'s `GraphBridgeAutomationLibrary.cpp` (10,107
lines) is larger than `main`'s (9,294 lines) at the point the histories
end. `archive/main-pre-5.8` is preserved for reference and provenance,
not because it holds anything `master` is missing.

See [`Archive/UE5.8_MIGRATION_NOTES.md`](Archive/UE5.8_MIGRATION_NOTES.md)
for `main`'s own account of its (separate, now-archived) UE 5.7→5.8
migration work — informational only; it does not describe how `master`
reached UE 5.8 support, which happened independently.
