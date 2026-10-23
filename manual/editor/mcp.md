# Connect an AI agent through MCP

FORGE can let a trusted AI agent inspect and edit your open scene through MCP.
The agent's changes appear in the editor and share your normal Undo history.
This first set of tools covers scene editing. Project management, asset import,
C++ compilation, Play controls and game export still require the editor.

## Connect

1. Open the project and scene you want the agent to work on.
2. Open **Tools → Automation → Local connection…**.
3. Choose **Start read only**, or **Start with scene edits** to allow changes.
4. Click **Copy MCP configuration**.
5. Add the copied FORGE server entry to your AI application's MCP configuration and start its connection.

The exact settings screen depends on the application. It must support local
stdio MCP servers and one of FORGE's supported protocol versions:
2026-07-28 or 2025-11-25.

The copied configuration points to FORGE's existing `bin/forge_tools.exe` and
includes a temporary access token. Keep it within the trusted AI client. FORGE
does not require Python or another scripting language to run this connection.
If the client uses a different configuration format, retain the executable,
`--mcp-stdio` argument and `FORGE_AUTOMATION_CONNECTION` environment setting.

Ask your agent to **discover FORGE's scene tools and describe the open scene**.
It should see a read-only or editable tool set matching your choice. To try an
edit, ask it to **create a cube named Agent Cube at the origin**. The cube should
appear in Hierarchy; Undo removes it. Save in FORGE to keep authored changes.

## Keep control

- **Stop connection** immediately revokes access. Already committed changes stay in your scene and can be undone.
- Switching scenes/projects or restarting the editor requires a new connection and freshly copied MCP configuration. Regular Save keeps the connection.
- Edits pause during Play, C++ builds, dialogs and active editor gestures.
- The agent can edit supported scene properties and create entities. These tools do not grant access to FORGE's engine source or unrelated computer files.

If the agent reports a lost response, it should inspect state or retrieve the
same command receipt before trying again. Repeating an uncertain action as a
new command can create duplicate objects. The tools provide revisions and
sequence numbers to help the agent handle this correctly.

The precise capability checklist and integration contract are maintained in
**docs/mcp.md** in the source repository.
