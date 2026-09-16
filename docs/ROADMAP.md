# Roadmap

## v2.0.1 (Current — Security Hardening)

**Status**: ✅ Complete and locked

Point release on top of v2.0.0. No command behavior changed and no new
tools were added — see CHANGELOG.md for the full list of security fixes
(per-connection auth state, use-after-free fix, credential storage moved
out of plaintext config, RUN_PYTHON off by default, and more).

v2.0.0 shipped:
- 129 fully-typed tools
- Tier 1+2 with parameter bounds, enums, defaults
- MCP server with dual-server coexistence

**Maintenance**: Bug fixes and critical updates only. No new features.

## v3.0.0 (Next — New Plugin)

**Status**: Planned (separate repository)

v3 will be shipped as a new plugin to allow major enhancements without breaking v2 deployments.

### v3 Planned Features
- **Full Type Coverage**: Tier 3 tools with complete enums and bounded parameters
- **Enhanced Animation**: Advanced montage composition, sync group management
- **Procedural Content**: PCG graph integration
- **Performance**: Batch operations for multi-node wiring
- **Extended MCP**: Additional Claude-native patterns

**Why a new plugin?**
- v2 is stable and locked; we won't break existing deployments
- v3 allows architectural changes not possible in v2
- Projects can maintain v2 while adopting v3 alongside it
- Clean separation of concerns

## Support Timeline

| Version | Status | Support |
|---------|--------|---------|
| **v2.0.1** | Current | Bug fixes, security patches |
| **v1.x** | Legacy | Critical security fixes only |
| **v3.0.0** | Future | Active development |

## Questions?

See [USAGE.md](USAGE.md) for quick-start guides, or report issues through
the support channel listed on the Fab product page.
