# Plan: Session Architecture — Connecting to Claude Sessions

## Context

The current `--sdk-url` spawn-and-control model spawns new Claude processes. However, `system/init` never arrives over the SDK WebSocket — the session hangs after plugin `SessionStart:startup` hooks run. The user's actual use case is monitoring and controlling existing Claude sessions from the 3DS.

## Status

The 3DS app bugs (slot hardcoding, ghost sessions, selected row visibility) have been fixed and the app builds. The session connection issue remains unresolved.

---

## Option A: Manual `--sdk-url` Connection

The user starts Claude themselves with `--sdk-url` pointed at the companion server.

**How it works:**
- Companion server does NOT auto-spawn sessions
- User runs: `claude --sdk-url ws://localhost:3333/ws/cli/0`
- CLI connects to the companion server, sends `system/init`, and the 3DS sees the session
- Companion server handles permissions (approve/deny from 3DS)
- Could add a shell alias: `alias clauder='claude --sdk-url ws://localhost:3333/ws/cli/0'`

**Pros:**
- Uses existing `--sdk-url` infrastructure (WebSocket handlers, permission flow)
- Full control: approve/deny permissions, send interrupts
- No plugin conflicts (user's normal environment)

**Cons:**
- User must remember to pass `--sdk-url` (mitigated by alias)
- Only monitors sessions started with the flag

---

## Option B: Hooks for Monitoring + Spawn for New Agents

Use hooks for observing the user's primary session, and the spawn model for creating additional agents.

**How it works:**
- Reinstall `PreToolUse`/`PostToolUse` hooks that POST to companion server
- Hooks provide status updates (tool name, state, pending permissions)
- 3DS shows the user's session as slot 0 via hook data
- Spawn feature creates additional controlled agents (slots 1-3) using `--sdk-url`
- For permission approval on the hooked session, need a different mechanism (hooks can't approve)

**Pros:**
- Works with any Claude session automatically
- No special flags needed to start Claude

**Cons:**
- Hooks can only observe, not control — can't approve/deny permissions from 3DS for hooked sessions
- Two different codepaths for monitoring vs spawned agents
- Need to solve the plugin blocking issue for spawned agents

---

## Option C: Fix the Spawn Model

Debug and fix why `system/init` doesn't arrive after plugin hooks.

**Investigation needed:**
- The `compound-engineering@every-marketplace` plugin's `SessionStart:startup` hook runs but may block
- After `hook_response`, no further messages arrive on the SDK WebSocket
- stdout (now piped) also shows no output
- Try: spawn with `CLAUDE_CODE_SKIP_PLUGINS=1` env var, or narrow down which plugin blocks

**Pros:**
- Keeps the current architecture as designed
- Full spawn-and-control capability

**Cons:**
- Fighting plugin compatibility issues
- Spawned sessions are separate from the user's own sessions
- User still needs a way to connect existing sessions

---

## Recommendation

**Option A** is the simplest path that matches the user's use case. It reuses the existing WebSocket infrastructure and provides full control. The spawn feature can be added later if needed.

## Files Already Changed (committed with this plan)

| File | Changes |
|------|---------|
| `3ds-app/source/network.h` | Slot param on send functions, simplified config |
| `3ds-app/source/network.c` | Slot param implementations |
| `3ds-app/source/main.c` | Updated call sites, active-agent nav, removed dead code |
| `3ds-app/source/ui.c` | Skip inactive agents, selected row contrast, text buffer 8192, removed dead fn |
| `3ds-app/source/ui.h` | Removed ui_touch_spawn declaration |
| `companion-server/src/server.ts` | Only broadcast active slots |
| `companion-server/src/cli-handler.ts` | Raw message debug logging |
| `companion-server/src/session.ts` | Stdout/stderr logging, stdin pipe |
