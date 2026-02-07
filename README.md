# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

**Status:** MVP in development

## Quick Start

### Prerequisites

- Docker and Docker Compose
- Bun (for local development)
- Nintendo 3DS with CFW or Citra emulator

### Companion Server

```bash
cd companion-server
bun install
bun run dev
```

The server runs on:
- HTTP :3333 (receives Claude Code hooks)
- WebSocket :3334 (3DS connection)

### 3DS App

```bash
# Build using Docker (no local devkitPro needed)
docker compose run --rm 3ds-build

# Output: 3ds-app/raids.3dsx
```

Before building, edit `3ds-app/source/config.h` with your dev machine's IP address.

### Install Claude Code Hooks

```bash
cd companion-server
bun run src/index.ts install
```

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │───── hooks ───────>│    Companion    │<==================>│     3DS     │
│   (terminal)    │<── tmux keys ──────│     Server      │      WiFi          │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
```

## Testing

```bash
# Run automated tests
./scripts/test-e2e.sh

# Manual WebSocket testing
wscat -c ws://localhost:3334
```

## Project Structure

```
rAI3DS/
├── 3ds-app/           # Nintendo 3DS homebrew app (C/libctru)
├── companion-server/  # Bridge server (Bun/TypeScript)
├── plans/             # Design documents
└── scripts/           # Development utilities
```

## License

Open source (license TBD)
