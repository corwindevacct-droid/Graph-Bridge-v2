# Troubleshooting

## Editor Crashes

**Problem**: Editor crashes on plugin load.

**Solution**:
1. Delete Saved/ and Intermediate/ directories
2. Delete .sln file and regenerate with Generate Visual Studio project files
3. Rebuild the plugin

## Port Already in Use

**Problem**: "Address already in use" error on :8080 or :8090.

**Solution**:
- Check if another GraphBridge instance is running
- Close all editor windows and wait 10 seconds
- Use netstat to find the offending process:
  ```bash
  netstat -ano | findstr :8080
  ```

## Bundled Python Client Not Found

**Problem**: `ImportError` for `graphbridge_bridge` or `graphbridge_config`.

**Solution**:
1. Verify `Content/Python/graphbridge_bridge.py` exists
2. Add the plugin's Python folder to `sys.path`:
   ```python
   import sys
   sys.path.insert(0, '/path/to/plugin/Content/Python')
   from graphbridge_bridge import UnrealBridge
   ```

## MCP Connection Failed

**Problem**: Cannot connect to MCP server on :8090.

**Solution**:
1. Verify the plugin is loaded in Editor
2. Check firewall settings allow localhost connections
3. Verify no other MCP server is using :8090
4. Restart the editor

## Tools Return "Not Found"

**Problem**: Commands like SPAWN_NODE return "ERR: Blueprint not found".

**Solution**:
- Verify the Blueprint path is correct: /Game/MyBlueprint
- The Blueprint must exist and be accessible in the project
- Use LIST_GRAPHS to discover available graphs first

## Python Version Mismatch

**Problem**: "Python 2.7 not supported" error.

**Solution**:
- GraphBridgev2 requires Python 3.9+
- Update your Python installation
- Verify `python --version` returns 3.9+

## Performance Issues

**Problem**: Tools respond slowly.

**Solution**:
- Check editor CPU/memory usage (expect 50-200ms per command)
- Reduce number of nodes in the Blueprint being modified
- Restart the editor if memory usage is high

## Still Stuck?

Check the logs:
- Editor Log: Saved/Logs/Bridge5_8.log
- MCP Log: Look for "GraphBridgeMCPServer" entries
- Python Log: Print debug info from your scripts

Report issues through the support channel listed on the Fab product page.
