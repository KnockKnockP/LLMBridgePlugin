# LLMBlueprintBridge

Unreal Engine 5.7 editor-only local bridge that lets LLM agents (Claude Code, Codex, any MCP client) inspect and edit Blueprints, StateTree, GAS, materials, animation, Niagara, Sequencer, UMG and other assets through structured editor APIs. It never edits raw `.uasset` files.

Packaged builds exclude it, because the module is `Editor` only.

## Installation

1. Clone or copy this repository into your project as `Plugins/LLMBlueprintBridge/`.
2. Install the required **[GitAssetDiff](https://github.com/KnockKnockP/GitDiffPlugin)** plugin as `Plugins/GitAssetDiff/`.

   ```
   git clone https://github.com/KnockKnockP/LLMBridgePlugin.git Plugins/LLMBlueprintBridge
   git clone https://github.com/KnockKnockP/GitDiffPlugin.git Plugins/GitAssetDiff
   ```

3. Enable both in your `.uproject` (or through **Edit → Plugins**):

   ```json
   { "Name": "GitAssetDiff", "Enabled": true },
   { "Name": "LLMBlueprintBridge", "Enabled": true }
   ```

4. Regenerate project files and build the editor target. The server starts with the editor when `Auto Start` is on in its settings.

### Dependencies

- Unreal Engine 5.7
- **[GitAssetDiff](https://github.com/KnockKnockP/GitDiffPlugin)** (separate repository): provides the Git revision loading used by the `*_git_revision` tools and the Content Browser **Diff → Against Git Revision...** menu.
- Engine plugins, all enabled automatically through the `.uplugin`: GameplayAbilities, GameplayTagsEditor, GameplayStateTree, StateTree, IKRig, Niagara, PropertyBindingUtils, SequencerScripting.

## Transports

Two transports serve the same tool set (about 188 tools).

**MCP (recommended)**: JSON-RPC 2.0 at `http://127.0.0.1:9877/mcp`. It implements `initialize`, `tools/list`, `tools/call` and `ping`, with protocol versions `2025-11-25`, `2025-06-18` and `2024-11-05`. It advertises tool input schemas, so agents do not need to read `Docs/API.md` first.

```
claude mcp add --transport http unreal http://127.0.0.1:9877/mcp
```

**REST (legacy, still supported)**: base `http://127.0.0.1:9877/llm-blueprint/v1`, with endpoints `GET /status`, `GET /tools` and `POST /invoke`.

```json
{"tool":"list_blueprints","arguments":{"path":"/Game","exclude_paths":["/Game/Art"],"limit":25}}
```

## Settings

Find them under **Project Settings → Plugins → LLM Blueprint Bridge**: auto start, port, `Enable MCP Endpoint`, `MCP Url Path`, and an optional `AuthToken`.

## Docs

| File | Contents |
| --- | --- |
| `Docs/API.md` | Compact tool schemas and return shapes |
| `Docs/AGENT_GUIDE.md` | Recommended agent workflow (inspect → mutate → compile → save) |
| `Docs/GIT_ASSET_DIFF.md` | Git revision tools (built on GitAssetDiff) |
| `Docs/PRIOR_ART.md` | Comparable projects and the Unreal APIs used |

## Agent guidance

**Node layout: place nodes by hand.** Inspect existing node positions and set explicit `NodePosX` / `NodePosY` through `set_property`. `format_blueprint_graph` (both `control_flow_parameters` and `dependency_layered`) is **deprecated** because automatic placement makes layouts that people can't read. The server rejects it unless the call passes `allow_deprecated_automatic_layout=true`.

**Token controls**: JSON responses are condensed, and Blueprint inspection returns graph summaries by default (`include_nodes=false`). To get more detail, pass `include_nodes`, `graph|graphs`, `include_pins`, `include_pin_types`, `include_pin_links` or `omit_empty`. Use scoped discovery (`path/query/limit/exclude_paths`) before inspection.

## Development

The tool catalog `Source/LLMBlueprintBridge/Private/LLMBlueprintBridgeToolCatalog.gen.inl` is generated from `Docs/API.md` and the `InvokeTool` dispatch. Regenerate it after changing either:

```
python Plugins/LLMBlueprintBridge/Tools/generate_tool_catalog.py
```

**UE 5.8 note**: Epic ships a first-party `ModelContextProtocol` plugin in 5.8, with tools registered through `IModelContextProtocolTool` / `UToolsetDefinition`. 5.7 has no such plugin, so this project has its own MCP layer. `ILLMBlueprintBridgeToolInvoker` is the seam: on 5.8 you can wrap the tools as `IModelContextProtocolTool` and drop this HTTP server.

## Security

The server accepts local requests only by default. When `AuthToken` is set, clients must send `X-LLM-Bridge-Token: <token>`. The API edits assets, so never expose the port to untrusted networks.

## License

MIT. See [LICENSE](LICENSE).
