# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

Inspired by:
- https://ralv.ai/
- https://vibecraft.sh/
- https://github.com/stevysmith/clawdgotchi

**Status:** MVP completed, developing functional improvements

## Goals

I love the 3DS, and I have always wanted to build a homebrew app. Vibe coding has both opened the door for me to do this, and provided an opportunity for what the app could be. I imagine a control interface akin to playing Pokémon! I feel the menuing and turn-based style lends itself well to this use case. The following are some goals that I hope to make a reality with this project:

- Connect to and control existing Claude Code sessions using the 3DS as a controller
- Spawn new agents
- Prompting with the 3DS mic (local whisper model running with server to interpret audio?)
- Nested menus with prompts, commands, skills, plugins, and more
- Add support for other agent providers (Cursor, Codex, Gemini, etc.)

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

The server runs on port 3333 (HTTP + WebSocket).

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
wscat -c ws://localhost:3333
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
