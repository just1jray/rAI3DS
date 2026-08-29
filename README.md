# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

Your 3DS becomes a permission remote for Claude Code — a real decision point, not a keyboard.

Inspired by:
- https://ralv.ai/
- https://vibecraft.sh/
- https://github.com/stevysmith/clawdgotchi

**Status:** MVP with HTTP hooks + FIGHT wheel (first navigational menu)

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

## FIGHT Menu (Current Feature)

The FIGHT wheel is a Mass Effect-style dialogue / Pokémon move-style menu for steering agents:

**Bottom Screen:**
- Shows 3-6 short generated options based on agent state and context
- D-pad (or Circle Pad) highlights options
- **A button**: Select option (soft - updates state, logs prompt)
- **B button**: RUN (soft stop - updates state, logs stop prompt)
- Touch: Large hitboxes for each option (tap to select)

**Top Screen:**
- Shows agent status, context usage, and last beat (recent activity)

**Option Generation:**
- Heuristic + template-based (no paid API required)
- Context-aware: adapts to current tool, file paths, commands
- State-aware: different options for idle, working, error, done states

**Soft-only:** pick/run update UI state and log prompts to console. No direct injection into Claude CLI (no tmux). Prompts are available for manual copy/paste or future input hook integration.

### Testing in Azahar (3DS Emulator)

**Important:** Use D-pad + A/B buttons as the primary controls. Touch hitboxes may be unreliable in the emulator.

1. Build the 3DS app: `docker compose run --rm 3ds-build`
2. Start companion server: `cd companion-server && bun run dev`
3. Load `3ds-app/raids.3dsx` in Azahar
4. Connect — **the FIGHT wheel must appear immediately** (3-6 options visible without any Claude hooks)
5. Navigate options with D-pad Up/Down
6. Press A to send the highlighted option
7. Press B to RUN (stop) the agent
8. L/R bumpers or D-pad Left/Right to switch between agents

#### Azahar Keyboard Bindings (CRITICAL)

Azahar's **default** keyboard mapping for 3DS buttons:
| 3DS Button | Default Keyboard Key |
|------------|---------------------|
| **A** | `A` |
| **B** | `S` (NOT keyboard B!) |
| **X** | `Q` |
| **Y** | `W` |
| D-pad | Arrow keys |
| L/R | `E` / `R` |

**For QA testing, you MUST rebind 3DS B → keyboard B:**
1. In Azahar, go to **Emulation → Configure → Controls**
2. Click on the B button mapping
3. Press keyboard `B` to rebind
4. Click OK to save

Without this rebind, pressing keyboard B will do nothing (it's not mapped), and pressing keyboard S will trigger 3DS B (RUN/stop).

**QA Verification Steps:**
1. Connect Azahar → FIGHT wheel shows 3-6 options immediately (no Claude session needed)
2. Press keyboard `A` → WS `pick` message sent, state updates
3. Press keyboard `B` (after rebind) → WS `run` message sent, "RUN sent!" flash
4. If no options (edge case), pressing A shows "NO MOVES!" flash instead of silent no-op

The bottom screen shows `[A ]` or `[B ]` hint when buttons are pressed to confirm input is arriving.

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
| **A** | Approve permission (yes) / Send FIGHT option |
| **B** | Deny permission (no) / RUN (stop) |
| **X** | Always approve this tool type |
| **Y** | Toggle auto-edit mode |
| **D-pad Up/Down** | Navigate FIGHT options |
| **D-pad Left/Right, L/R** | Switch selected agent |

## Testing

```bash
# Run the test script (proves PermissionRequest hold-queue works)
./scripts/test-permission-hook.sh

# Test FIGHT wheel protocol (options, pick, run)
./scripts/test-fight-wheel.sh

# Manual WebSocket testing
wscat -c ws://localhost:3333
# Send: {"type":"action","agent":"claude","action":"yes","slot":0}
# Test pick/run:
# {"type":"pick","slot":0,"index":0}
# {"type":"run","slot":0}
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
│   │   ├── options.ts # FIGHT wheel option generator
│   │   └── context.ts # Context tracking
│   └── raids.sh       # Launcher script
├── plans/             # Design documents
└── scripts/           # Development utilities
```

## License

Open source (license TBD)
