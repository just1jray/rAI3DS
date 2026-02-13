# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

**Status:** MVP in development

## Quick Start

### Prerequisites

- Docker and Docker Compose
- Bun (for local development)
- Claude Code CLI (`claude` command available on PATH)
- Nintendo 3DS with CFW or Citra emulator

### Companion Server

```bash
cd companion-server
bun install
bun run src/index.ts
```

The server runs on port 3333 (HTTP + WebSocket). On startup it automatically spawns a Claude Code subprocess in slot 0 using `--sdk-url`.

### 3DS App

```bash
# Build using Docker (no local devkitPro needed)
docker compose run --rm 3ds-build

# Output: 3ds-app/raids.3dsx
```

Before building, edit `3ds-app/source/config.h` with your dev machine's IP address.

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │<== NDJSON/WS =====>│    Companion    │<==================>│     3DS     │
│  (subprocess)   │   --sdk-url        │     Server      │      WiFi/WS      │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
```

The companion server acts as a bridge between Claude Code and the 3DS:

- **CLI side:** Claude Code connects via `--sdk-url ws://localhost:3333/ws/cli/{slot}`, sending structured NDJSON messages (assistant responses, permission requests, results, tool progress)
- **3DS side:** The 3DS app connects over WiFi to the same port, receiving JSON status updates and sending user actions (approve/deny/interrupt)

### Key flows

| Flow | CLI → Server → 3DS |
|------|---------------------|
| Permission prompt | `control_request` → `agent_status {waiting}` → user presses A/B/X → `control_response` back to CLI |
| Agent working | `assistant` with usage → `agent_status {working, contextPercent}` |
| Tool running | `tool_progress` → `agent_status {working, "Running: Bash (5s)"}` |
| Interrupt | 3DS escape → `control_request {interrupt}` to CLI |
| Auto-edit | `control_request` for Edit/Write → auto-approved without 3DS prompt |

## Project Structure

```
rAI3DS/
├── 3ds-app/           # Nintendo 3DS homebrew app (C/libctru)
├── companion-server/  # Bridge server (Bun/TypeScript)
│   └── src/
│       ├── index.ts        # Entry point
│       ├── server.ts       # HTTP + dual WebSocket server
│       ├── cli-handler.ts  # NDJSON message dispatch & permission flow
│       ├── session.ts      # Subprocess lifecycle (Bun.spawn)
│       └── types.ts        # SDK + 3DS protocol types
├── plans/             # Design documents
└── scripts/           # Development utilities
```

## Testing

```bash
# Verify server starts without errors
bun run companion-server/src/index.ts --help

# Start server (spawns Claude Code subprocess)
bun run companion-server/src/index.ts

# Manual WebSocket testing (3DS protocol)
wscat -c ws://localhost:3333

# Manual CLI testing (SDK protocol)
claude --sdk-url ws://localhost:3333/ws/cli/0 --print --output-format stream-json --input-format stream-json --verbose -p ""
```

## License

Open source (license TBD)
