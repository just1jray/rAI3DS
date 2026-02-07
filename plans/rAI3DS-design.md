# rAI3DS - 3DS Companion for Vibe Coding

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

## Project Overview

**Name:** rAI3DS (pronounced "raids")
**License:** Open source from day one

### Core Goals
1. **Context switching** - Dedicated device for approvals keeps hands free
2. **Awareness** - Always know when agents need attention vs. are working
3. **Gamification** - Make vibe coding fun with character animations and visual feedback

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │───── hooks ───────>│    Companion    │<==================>│     3DS     │
│   (terminal)    │<── stdin/tmux ─────│     Server      │      WiFi          │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
                                              │
                                              v
                                       ┌─────────────────┐
                                       │  Agent Adapters │
                                       │  Claude, Codex, │
                                       │  Gemini, Cursor │
                                       └─────────────────┘
```

### Components

| Component | Technology | Purpose |
|-----------|------------|---------|
| 3DS App | C/C++ with devkitPro/libctru | Handheld interface |
| Companion Server | Node.js/TypeScript (Bun) | Bridge between 3DS and agents |
| Agent Adapters | Plugin modules | Translate agent-specific hooks to common protocol |

### Why This Architecture
- **Hooks** are officially supported by all target agents (Claude Code, Codex, Gemini CLI, Cursor)
- **Companion server** is agent-agnostic - swap adapters without changing 3DS app
- **WiFi** enables bidirectional rich data (status → 3DS, commands → agents)
- **Local only** - no cloud services, code stays on your machine

## 3DS App Design

### Top Screen (400x240) - Multi-Agent Dashboard

All agents visible simultaneously with health-bar style progress indicators:

```
┌─────────────────────────────────────────────────────────┐
│  CLAUDE CODE        ████████████░░░░ 75%  [Working]     │
│  ┌────┐                                                 │
│  │ 🔨 │  <- mini character animation                    │
│  └────┘                                                 │
│─────────────────────────────────────────────────────────│
│  CODEX              ████████░░░░░░░░ 50%  [Waiting]     │
│  ┌────┐                                                 │
│  │ 👀 │  <- looking at you (needs approval)             │
│  └────┘                                                 │
│─────────────────────────────────────────────────────────│
│  GEMINI             ░░░░░░░░░░░░░░░░  --  [Idle]        │
│  ┌────┐                                                 │
│  │ 💤 │  <- sleeping/resting                            │
│  └────┘                                                 │
│─────────────────────────────────────────────────────────│
│  CURSOR             ████████████████  ✓  [Done!]        │
│  ┌────┐                                                 │
│  │ 🎉 │  <- celebrating                                 │
│  └────┘                                                 │
└─────────────────────────────────────────────────────────┘
```

### Bottom Screen (320x240) - Pokemon-Style Touch UI

```
┌─────────────────────────────────────────────────────────┐
│  ┌─────────────────────────────┐  ┌──────────────────┐  │
│  │          APPROVE            │  │      DENY        │  │
│  │            ✓                │  │        ✗         │  │
│  └─────────────────────────────┘  └──────────────────┘  │
│                                                         │
│  ┌─────────────────────────────────────────────────────┐│
│  │    "Claude wants to run: npm install express"       ││
│  └─────────────────────────────────────────────────────┘│
│                                                         │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌────────────┐  │
│  │ /commit  │ │  /clear  │ │  Macros  │ │  Settings  │  │
│  └──────────┘ └──────────┘ └──────────┘ └────────────┘  │
│                                                         │
│  [Claude ▼]  [Codex]  [Gemini]  [Cursor]                │
└─────────────────────────────────────────────────────────┘
```

**Layout Zones:**
1. **Primary Actions** - Big approve/deny buttons (context-aware)
2. **Context Area** - Shows what's being approved or current status
3. **Quick Actions** - Common commands, macros menu, settings
4. **Agent Selector** - Tap to switch which agent you're controlling

**Context-Aware Behavior:**

| Agent State | Primary Zone Shows |
|-------------|-------------------|
| Waiting for approval | Approve / Deny buttons |
| Working | Cancel button or motivational message |
| Idle | "Send prompt" button, recent commands |
| Error | Retry / Dismiss buttons |

### Character System

**Default Theme:** Pixel Worker (pixel art character at workbench)

**Character States:**
- **Working** - Hammering, typing, tools moving
- **Waiting** - Looking at you, tapping foot, arms crossed
- **Celebrating** - Arms up, confetti, victory pose
- **Confused** - Scratching head, question marks (disconnected/error)
- **Idle** - Resting, stretching, yawning

**Future Themes:** Robot, Wizard, Cat, and user-contributed themes

## Companion Server Design

### Tech Stack
- Runtime: Bun (Node.js compatible, faster)
- Language: TypeScript
- WebSocket: `ws` library for 3DS communication
- Configuration: JSON config files

### Protocol (JSON over WebSocket)

```typescript
// Server → 3DS: Agent status update
interface AgentStatus {
  type: "agent_status";
  agent: "claude" | "codex" | "gemini" | "cursor";
  state: "working" | "waiting_approval" | "idle" | "error" | "done";
  progress: number; // 0-100, -1 for indeterminate
  message: string;
  details?: {
    command?: string;
    file?: string;
    risk?: "low" | "medium" | "high";
  };
}

// 3DS → Server: User action
interface UserAction {
  type: "action";
  agent: string;
  action: "approve" | "deny" | "cancel";
}

// 3DS → Server: Send command
interface SendCommand {
  type: "command";
  agent: string;
  command: string; // e.g., "/commit", "/clear", custom macro
}
```

### Agent Adapters

Each adapter implements:
```typescript
interface AgentAdapter {
  name: string;
  install(): Promise<void>;      // Set up hooks for this agent
  uninstall(): Promise<void>;    // Remove hooks
  sendApproval(): Promise<void>; // Send approve to agent
  sendDenial(): Promise<void>;   // Send deny to agent
  sendCommand(cmd: string): Promise<void>;
}
```

### Connection Handling

- **Auto-reconnect:** Retry every 2 seconds when disconnected
- **Action queue:** Max 3 actions per agent, expires after 30 seconds
- **Debouncing:** Ignore duplicate actions within 500ms (prevents frustrated tapping)
- **Visual feedback:** 3DS shows "Reconnecting..." and queued action count

## Development Phases

### Phase 1: MVP
**Goal:** Working end-to-end demo with Claude Code

- [ ] Set up devkitPro toolchain and hello world 3DS app
- [ ] Create basic 3DS UI (top screen status, bottom screen buttons)
- [ ] Build companion server with WebSocket
- [ ] Implement Claude Code adapter using hooks
- [ ] WiFi connection with auto-reconnect
- [ ] Approve/Deny flow working end-to-end
- [ ] Pixel Worker character (static or simple animation)

### Phase 2: Multi-Agent Support
**Goal:** Support all target agents

- [ ] Add Codex CLI adapter
- [ ] Add Gemini CLI adapter
- [ ] Add Cursor adapter
- [ ] Multi-agent view on top screen
- [ ] Agent switching on bottom screen

### Phase 3: Customization
**Goal:** User personalization

- [ ] Custom macros system
- [ ] Context-aware button layouts
- [ ] Settings screen on 3DS
- [ ] Additional character themes
- [ ] Sound effects (optional, default off)

### Phase 4: Gamification
**Goal:** Make it fun

- [ ] XP/progress tracking system
- [ ] Unlockable themes as rewards
- [ ] Streaks and achievements
- [ ] Evolving visual elements
- [ ] Leaderboards (optional)

## Project Structure

```
rAI3DS/
├── 3ds-app/                 # 3DS homebrew application
│   ├── source/
│   │   ├── main.c           # Entry point
│   │   ├── ui/              # Screen rendering
│   │   ├── network/         # WiFi/WebSocket client
│   │   └── assets/          # Sprites, fonts
│   ├── Makefile
│   └── README.md
│
├── companion-server/        # Node.js/TypeScript server
│   ├── src/
│   │   ├── index.ts         # Entry point
│   │   ├── server.ts        # WebSocket server
│   │   ├── state.ts         # State management
│   │   └── adapters/
│   │       ├── claude.ts    # Claude Code adapter
│   │       ├── codex.ts     # Codex adapter
│   │       ├── gemini.ts    # Gemini CLI adapter
│   │       └── cursor.ts    # Cursor adapter
│   ├── package.json
│   └── README.md
│
├── docs/
│   ├── setup-guide.md       # Getting started
│   ├── creating-themes.md   # How to add character themes
│   └── creating-adapters.md # How to add agent adapters
│
└── README.md                # Project overview
```

## Getting Started (For Development)

### Prerequisites
- Nintendo 3DS with custom firmware (Luma3DS)
- devkitPro with libctru installed
- Bun (or Node.js 20+)
- Claude Code installed

### 3DS Development Setup
```bash
# Install devkitPro (macOS)
brew install devkitpro-pacman
sudo dkp-pacman -S 3ds-dev

# Build 3DS app
cd 3ds-app
make

# Install to 3DS via FBI or copy to SD card
```

### Companion Server Setup
```bash
cd companion-server
bun install
bun run dev
```

## Verification

### MVP Testing Checklist
1. [ ] 3DS app launches without crash
2. [ ] 3DS connects to companion server over WiFi
3. [ ] Claude Code hooks fire and send status to server
4. [ ] Status appears on 3DS top screen
5. [ ] Tapping Approve on 3DS approves action in Claude Code
6. [ ] Tapping Deny on 3DS denies action in Claude Code
7. [ ] Disconnection shows "confused" character, reconnects automatically
8. [ ] Queued actions send after reconnection

### Testing Commands
```bash
# Start companion server
cd companion-server && bun run dev

# In another terminal, start Claude Code with test task
claude "create a new file called test.txt with hello world"

# 3DS should show approval request
# Tap approve on 3DS
# Verify file was created
```

## Resources

### 3DS Homebrew Development
- [devkitPro Getting Started](https://devkitpro.org/wiki/Getting_Started)
- [libctru Documentation](https://libctru.devkitpro.org/)
- [3DS Homebrew Examples](https://github.com/devkitPro/3ds-examples)

### Agent Hook Documentation
- [Claude Code Hooks](https://docs.anthropic.com/claude-code/hooks)
- [Codex CLI Hooks](https://github.com/openai/codex)
- [Cursor Hooks](https://cursor.com/docs/agent/hooks)
- [Gemini CLI Hooks](https://geminicli.com/docs/hooks/)

### Inspiration
- [ralv.ai](https://ralv.ai/) - RTS-style multi-agent interface
- [vibecraft.sh](https://vibecraft.sh/) - 3D Claude Code visualization
