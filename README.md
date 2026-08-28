# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

Your 3DS becomes a permission remote for Claude Code — a real decision point, not a keyboard.

Inspired by:
- https://ralv.ai/
- https://vibecraft.sh/
- https://github.com/stevysmith/clawdgotchi

**Status:** MVP completed with HTTP hook architecture (no tmux required)

## How It Works

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │──[HTTP hooks]─────>│    Companion    │<══[WebSocket]═════>│     3DS     │
│   (any shell)   │<───[response]──────│     Server      │                    │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
```

1. Run `raids install` to set up HTTP hooks in Claude Code
2. Run `raids` to start the companion server
3. Run `claude` in any terminal — the session registers via `SessionStart` hook
4. When Claude needs permission, the `PermissionRequest` hook **holds the HTTP response**
5. Your 3DS sees `STATE_WAITING` with tool details
6. Press **A** (yes), **B** (no), or **X** (always) on the 3DS
7. The held HTTP response resolves with allow/deny, Claude continues

**No tmux required.** No screen scraping. Real permission control via Claude Code's first-class hooks.

## Goals

I love the 3DS, and I have always wanted to build a homebrew app. Vibe coding has both opened the door for me to do this, and provided an opportunity for what the app could be. I imagine a control interface akin to playing Pokémon! I feel the menuing and turn-based style lends itself well to this use case.

- Connect to and control Claude Code sessions using the 3DS as a permission remote
- Real approve/deny decisions via HTTP hooks (not tmux key injection)
- Multiple agent slots (up to 4)
- Prompting with the 3DS mic (future: local whisper model)
- Nested menus with prompts, commands, skills, plugins, and more
- Add support for other agent providers (Cursor, Codex, Gemini, etc.)

## Quick Start

### Prerequisites

- [Bun](https://bun.sh/) (for the companion server)
- Nintendo 3DS with CFW or [Azahar](https://azahar-emu.org/) emulator
- Docker (optional, for building 3DS app)

### 1. Install Hooks and Start Server

```bash
cd companion-server
bun install
./raids.sh install
```

This installs HTTP hooks to `~/.claude/settings.json` and starts the server on port 3333.

### 2. Run Claude Code

```bash
# In any terminal — no tmux needed!
claude
```

Claude registers via the `SessionStart` hook. When a permission prompt appears, the `PermissionRequest` hook holds until your 3DS responds.

### 3. Build and Run the 3DS App

```bash
# Edit config.h with your dev machine's IP address
vim 3ds-app/source/config.h

# Build using Docker (no local devkitPro needed)
docker compose run --rm 3ds-build

# Output: 3ds-app/raids.3dsx
```

Copy `raids.3dsx` to your 3DS SD card or run it in Azahar.

## Architecture

### Hook Types

| Hook | Purpose |
|------|---------|
| `PermissionRequest` | **Primary permission control.** Holds HTTP response until 3DS responds. Uses `hookSpecificOutput.decision.behavior` (allow/deny). |
| `SessionStart` | Registers Claude session — no tmux session management required. |
| `SessionEnd` | Cleans up session state. |
| `PreToolUse` | Observability only (no permission control since Aug 2026). |
| `PostToolUse` | Updates UI state after tool completes. |
| `Stop` | Updates UI when Claude finishes responding. |

### Key Endpoints

| Endpoint | Purpose |
|----------|---------|
| `POST /hook/permission-request` | Holds until 3DS action resolves it (90s timeout → deny) |
| `POST /hook/session-start` | Registers new Claude session |
| `POST /hook/session-end` | Cleans up session |
| `GET /health` | Server status, pending permissions, connected clients |
| `WS /` | 3DS WebSocket connection |

### 3DS Controls

| Button | Action |
|--------|--------|
| **A** | Approve permission (yes) |
| **B** | Deny permission (no) |
| **X** | Always approve this tool type |
| **Y** | Toggle auto-edit mode |
| **D-pad/L/R** | Switch selected agent |

## Testing

```bash
# Run the test script (proves PermissionRequest hold-queue works)
./scripts/test-permission-hook.sh

# Manual WebSocket testing
wscat -c ws://localhost:3333
# Send: {"type":"action","agent":"claude","action":"yes","slot":0}
```

## Project Structure

```
rAI3DS/
├── 3ds-app/           # Nintendo 3DS homebrew app (C/libctru)
├── companion-server/  # Bridge server (Bun/TypeScript)
│   ├── src/
│   │   ├── index.ts   # Entry point
│   │   ├── server.ts  # HTTP hooks + WebSocket + permission hold-queue
│   │   ├── hooks.ts   # Hook installer (HTTP hooks)
│   │   ├── types.ts   # TypeScript types
│   │   └── context.ts # Context tracking
│   └── raids.sh       # Launcher script
├── plans/             # Design documents
└── scripts/           # Development utilities
```

## License

Open source (license TBD)
