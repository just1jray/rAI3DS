# rAI3DS Current Architecture

*Researched: 2026-03-27*

## Overview

rAI3DS is a Nintendo 3DS homebrew app that acts as a remote control for Claude Code AI agent sessions running on a dev machine.

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │───── hooks ───────>│    Companion    │<==================>│     3DS     │
│   (tmux pane)   │<── tmux send-keys ─│     Server      │      WiFi WS       │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
```

## Component Breakdown

### 1. Claude Code (terminal side)

Claude Code runs in a **tmux session** named `claude-raids`. Hooks are installed via `~/.claude/settings.json`:

```json
{
  "hooks": {
    "PreToolUse":  [{ "matcher": "", "hooks": [{ "type": "command", "command": "curl ... /hook/pre-tool" }] }],
    "PostToolUse": [{ "matcher": "", "hooks": [{ "type": "command", "command": "curl ... /hook/post-tool" }] }],
    "Stop":        [{ "matcher": "", "hooks": [{ "type": "command", "command": "curl ... /hook/stop" }] }],
    "UserPromptSubmit": [{ ... }]
  }
}
```

- Hooks are `type: "command"` — fire-and-forget `curl` POST calls to the companion server
- The hooks return immediately (no blocking), so hook data and tmux scraping run in parallel

### 2. Companion Server (`companion-server/`)

A **Bun/TypeScript** HTTP + WebSocket server on port 3333.

**Key subsystems:**

| Module | Responsibility |
|--------|---------------|
| `server.ts` | HTTP endpoints for hooks + WebSocket hub |
| `session.ts` | Maps slot numbers to tmux sessions |
| `adapters/claude.ts` | Sends keystrokes via `tmux send-keys` |
| `scraper.ts` | Polls tmux pane every 300ms for approval prompts |
| `hooks.ts` | Installs/uninstalls hooks in `~/.claude/settings.json` |
| `context.ts` | Tracks context window usage |

**Data flow:**

```
Claude Code → curl POST /hook/pre-tool
                    ↓
          pendingToolData.set(slot, {toolType, toolDetail})
          updateState(slot, { state: "working", message })
          broadcast({ type: "agent_status", ... }) to 3DS
          ← respond 200 immediately

scraper polls tmux every 300ms
  if "Do you want to proceed?" detected:
    updateState(slot, { state: "waiting", ... })
    ← 3DS sends { type: "action", action: "yes" }
    → tmux send-keys Enter        (for Yes)
    → tmux send-keys "Down Enter" (for No)
```

**The dual-source problem:**
- Hooks provide rich structured data (tool name, input parameters)
- Scraper detects when Claude is actually *waiting* for a decision
- Both run and try to update state independently
- The server tries to reconcile these (hooks set `pendingToolData`, scraper reads it)

### 3. 3DS App (`3ds-app/`)

Written in **C** using **libctru** + **citro2d**.

- Connects to companion server via custom WebSocket implementation
- Receives `agent_status` messages over WiFi
- Displays Pokémon-style UI: creature animation + status panel
- Up to 4 "party slots" (agents)
- Sends back `{ type: "action", action: "yes"|"no"|"always"|"escape" }` messages
- Reconnects automatically every 2 seconds if disconnected

## Known Weaknesses

### 1. tmux Screen Scraping is Fragile

`scraper.ts` searches for `"Do you want to proceed?"` in terminal output, then reverse-parses unicode box-drawing characters to extract tool name and detail. This breaks when:
- Claude Code changes its UI
- Terminal width/wrapping affects box rendering
- ANSI escape codes disrupt parsing

### 2. Approval is Positional (Keystroke Blind)

The adapter sends `Enter` for Yes, `Down Enter` for No. This assumes a fixed menu layout. If Claude Code changes menu ordering, approvals break silently.

### 3. PreToolUse Hook Doesn't Block

The current PreToolUse hook returns `{ action: "approve" }` immediately. Claude Code proceeds with the tool, then *separately* the scraper detects the approval prompt. This means:
- There's a race condition between hook arrival and scraper detection
- The hook can't actually block the tool from running based on 3DS input

### 4. No `SessionStart`/`SessionEnd` Hook Registered

These hooks exist in the type definitions but are not installed by default, so new sessions don't auto-register.

## Session Management

- Up to 4 slots (0–3), each mapping to a tmux session
- Slot 0 is the default "claude-raids" session (pre-existing)
- Slots 1–3 can be spawned from the 3DS (creates new tmux sessions)
- Session IDs from Claude Code hooks are lazily linked to slots
- Health check every 30s cleans up dead tmux sessions
