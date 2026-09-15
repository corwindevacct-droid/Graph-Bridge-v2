# Roadmap

## v2.0.0 (Current — Final Release)

**Status**: ✅ Complete and locked

- 129 fully-typed tools
- Tier 1+2 with parameter bounds, enums, defaults
- Python toolset auto-generated from manifest
- MCP server with dual-server coexistence
- Comprehensive test harness

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
| **v2.0.0** | Current | Bug fixes, security patches |
| **v1.0.0** | Legacy | Critical security fixes only |
| **v3.0.0** | Future | Active development |

## How to Contribute

- **v2**: Report issues, request documentation improvements
- **v3**: Will accept feature PRs in the v3 repository when it launches

## Questions?

See [USAGE.md](USAGE.md) for quick-start guides or file an issue on GitHub.
