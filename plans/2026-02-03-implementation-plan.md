# rAI3DS Implementation Plan

**Date:** 2026-02-03
**Status:** Ready for implementation
**Target:** Phase 1 MVP (Claude Code only)

## Development Approach

- **Parallel development:** Building 3DS app and companion server simultaneously
- **Testing:** Citra emulator for rapid iteration, real 3DS hardware for validation
- **Hook installation:** Automatic via companion server CLI

---

## Project Structure

```
rAI3DS/
├── 3ds-app/                    # C/C++ homebrew app
│   ├── source/
│   │   ├── main.c              # Entry point, main loop
│   │   ├── ui.c/h              # Screen rendering (citro2d)
│   │   ├── network.c/h         # WiFi + WebSocket client
│   │   ├── protocol.c/h        # JSON message parsing
│   │   └── sprites.c/h         # Character animations
│   ├── gfx/                    # Raw sprite assets (PNG)
│   ├── Makefile
│   └── README.md
│
├── companion-server/           # Bun/TypeScript server
│   ├── src/
│   │   ├── index.ts            # CLI entry point
│   │   ├── server.ts           # WebSocket server
│   │   ├── state.ts            # Agent state management
│   │   ├── hooks.ts            # Hook installer/manager
│   │   └── adapters/
│   │       └── claude.ts       # Claude Code adapter
│   ├── package.json
│   ├── tsconfig.json
│   └── README.md
│
├── plans/                      # Design docs
└── scripts/
    └── setup-devkitpro.sh      # Automated toolchain setup
```

**Key technology choices:**
- **citro2d** for 3DS 2D rendering (simpler than raw GPU)
- **cJSON** for JSON parsing on 3DS (small, portable)
- **Custom WebSocket client** on 3DS (no library available)
- **Bun** for companion server (fast, TypeScript native)

---

## Companion Server Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Companion Server                          │
│                                                              │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐   │
│  │  HTTP API    │───>│    State     │<───│  WebSocket   │   │
│  │  (hooks)     │    │   Manager    │    │  (3DS)       │   │
│  │  :3333       │    │              │    │  :3334       │   │
│  └──────────────┘    └──────────────┘    └──────────────┘   │
│         ^                   │                   │            │
└─────────│───────────────────│───────────────────│────────────┘
          │                   v                   v
    Claude Code         Agent State           3DS App
      hooks            { claude: {            (WebSocket
                         state,               client)
                         pending } }
```

### HTTP Endpoints (for Claude Code hooks)

| Endpoint | Purpose |
|----------|---------|
| `POST /hook/pre-tool` | Tool execution starting |
| `POST /hook/post-tool` | Tool execution finished |
| `POST /hook/prompt` | Waiting for user input |
| `POST /hook/response` | Agent responded |

### WebSocket Protocol

**Server → 3DS:**
```typescript
interface AgentStatus {
  type: "agent_status";
  agent: "claude" | "codex" | "gemini" | "cursor";
  state: "working" | "waiting" | "idle" | "error" | "done";
  progress: number;  // 0-100, -1 for indeterminate
  message: string;
  pending_command?: string;
}
```

**3DS → Server:**
```typescript
interface UserAction {
  type: "action";
  agent: string;
  action: "approve" | "deny" | "cancel";
}

interface SendCommand {
  type: "command";
  agent: string;
  command: string;
}
```

### Approval Mechanism

Claude Code runs inside a tmux session managed by the companion server:

1. Server starts: `tmux new-session -d -s claude "claude"`
2. Hooks notify server of state changes
3. When approval needed, server updates 3DS
4. 3DS sends approve → server runs `tmux send-keys -t claude "y" Enter`
5. 3DS sends deny → server runs `tmux send-keys -t claude "n" Enter`

---

## 3DS App Architecture

### Main Loop

```c
while (aptMainLoop()) {
    hidScanInput();

    // Handle touch input (bottom screen)
    if (touch_in_approve_button()) send_approve();
    if (touch_in_deny_button()) send_deny();

    // Network: non-blocking receive
    ws_poll();

    // Update character animations
    update_animations(delta_time);

    // Render
    C3D_FrameBegin();
    render_top_screen();    // Agent dashboard
    render_bottom_screen(); // Touch controls
    C3D_FrameEnd();
}
```

### Screen Layout

**Top Screen (400x240):** Agent dashboard with up to 4 rows
- Mini character sprite (animated)
- Progress bar
- Status text

**Bottom Screen (320x240):** Touch controls
- Large Approve/Deny buttons
- Context area (what's being approved)
- Agent selector tabs

### State Management

```c
typedef struct {
    char name[32];
    AgentState state;  // WORKING, WAITING, IDLE, ERROR, DONE
    int progress;      // 0-100, -1 for indeterminate
    char message[128];
    char pending_command[256];
} Agent;

Agent agents[4];
int selected_agent = 0;
```

### Network

- Custom lightweight WebSocket client over raw sockets
- Non-blocking reads in main loop
- Auto-reconnect with 2-second retry
- JSON parsing via cJSON library

---

## Claude Code Hook Configuration

Installed to `~/.claude/settings.json`:

```json
{
  "hooks": {
    "PreToolUse": [{
      "matcher": "*",
      "command": "curl -s -X POST http://localhost:3333/hook/pre-tool -H 'Content-Type: application/json' -d '{\"tool\":\"$CLAUDE_TOOL_NAME\"}'"
    }],
    "PostToolUse": [{
      "matcher": "*",
      "command": "curl -s -X POST http://localhost:3333/hook/post-tool -H 'Content-Type: application/json' -d '{\"tool\":\"$CLAUDE_TOOL_NAME\"}'"
    }]
  }
}
```

---

## Implementation Tasks

### Phase 1A: Companion Server

1. [ ] Scaffold Bun project with TypeScript config
2. [ ] HTTP server with hook endpoints
3. [ ] WebSocket server for 3DS connections
4. [ ] State manager for agent tracking
5. [ ] Claude adapter with tmux session management
6. [ ] `raids install` CLI command for hook setup
7. [ ] `raids start` CLI command to launch server + Claude session

### Phase 1B: 3DS App

1. [ ] Set up devkitPro toolchain (script)
2. [ ] Install Citra emulator
3. [ ] Hello world app with citro2d
4. [ ] Top screen: single agent status display
5. [ ] Bottom screen: touch buttons (Approve/Deny)
6. [ ] WebSocket client with auto-reconnect
7. [ ] Wire button presses to send actions
8. [ ] Basic character sprite (static for MVP)

---

## Testing Strategy

| Stage | Method | What to Verify |
|-------|--------|----------------|
| Server standalone | `wscat` as mock 3DS | Hook reception, state updates, WS messages |
| 3DS in Citra | Connect to localhost server | UI rendering, touch input, WS connection |
| End-to-end | Server + Citra + Claude Code | Full approve/deny flow |
| Real hardware | 3DS over WiFi | Same as above, network reliability |

### Test Commands

```bash
# Start companion server
cd companion-server && bun run dev

# Mock 3DS client
wscat -c ws://localhost:3334

# Test hook endpoint
curl -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}'
```

---

## MVP Success Criteria

From the design document:

- [ ] 3DS app launches without crash
- [ ] 3DS connects to companion server over WiFi
- [ ] Claude Code hooks fire and send status to server
- [ ] Status appears on 3DS top screen
- [ ] Tapping Approve on 3DS approves action in Claude Code
- [ ] Tapping Deny on 3DS denies action in Claude Code
- [ ] Disconnection shows reconnecting state, auto-reconnects
- [ ] Queued actions send after reconnection

---

## Dependencies & Setup

### devkitPro (3DS toolchain)

```bash
# macOS
brew install --cask devkitpro-pacman
sudo dkp-pacman -S 3ds-dev

# Verify
$DEVKITPRO/devkitARM/bin/arm-none-eabi-gcc --version
```

### Citra Emulator

Download from https://citra-emu.org/ or:
```bash
brew install --cask citra
```

### Companion Server

```bash
cd companion-server
bun install
```

---

## Resources

- [devkitPro 3DS examples](https://github.com/devkitPro/3ds-examples)
- [libctru documentation](https://libctru.devkitpro.org/)
- [citro2d documentation](https://citro2d.devkitpro.org/)
- [cJSON library](https://github.com/DaveGamble/cJSON)
- [Claude Code hooks](https://docs.anthropic.com/claude-code/hooks)
