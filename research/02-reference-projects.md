# Reference Project Analysis

*Researched: 2026-03-27*

## ClawdGotchi

**Repo:** `stevysmith/clawdgotchi`
**Concept:** macOS menubar Tamagotchi that monitors Claude Code sessions — animated crabs, one per repo.

### Architecture

```
Claude Code hooks → Python hook script → Unix socket /tmp/claudegotchi.sock
                                              ↓
                                     Electron main process
                                         sessionManager.ts
                                              ↓
                                        IPC → React UI
                                   (tray icon + popup window)
```

### Key Design Patterns

**Unix socket transport (not HTTP, not WebSocket):**
```typescript
// socketServer.ts
const server = net.createServer(socket => { ... })
server.listen('/tmp/claudegotchi.sock', () => {
  fs.chmodSync('/tmp/claudegotchi.sock', 0o777)
})
```
- Newline-delimited JSON over Unix domain socket
- Lower overhead than HTTP, simpler than WebSocket for local IPC
- Hook script is Python (not curl) — reads stdin, writes to socket

**Hook script invocation pattern:**
```json
{
  "matcher": "*",
  "hooks": [{ "type": "command", "command": "python3 ~/.claude/hooks/claudegotchi_hook.py" }]
}
```

**Session state machine:**
- `active` → `idle` (5 minutes no activity) → `ending` (3-second animation) → removed
- Health metrics: energy, health, happiness, discipline (gamification!)
- Deterministic accessory assignment by hashing repo path (same repo = same crab)

**IPC pattern (Electron):**
- `'sessions-updated'` events pushed from main to renderer
- `'get-health'` / `'get-sessions'` handlers for renderer pulls
- Window size scales dynamically with session count

### Lessons for rAI3DS

- Health/gamification metrics are compelling — extend beyond just "working/waiting/idle"
- Unix socket avoids HTTP overhead for local IPC
- Deterministic per-session identity (hashing repo name → slot assignment) is solid
- The 3-second "ending" animation gives visual feedback before session removal

---

## Vibecraft

**Repo:** `Nearcyan/vibecraft`
**Concept:** Manage Claude Code in a 3D workshop visualization — up to 6 simultaneous Claude instances.

### Architecture

```
Claude Code hooks → vibecraft-hook.sh → JSONL events file + WebSocket notify (optional)
                                                ↓
                                    server/index.ts (WebSocket server, port 4003)
                                         ↓             ↓
                                   tmux sessions    REST API (spawn/kill/prompt)
                                                        ↓
                                         Three.js browser client
```

### Key Design Patterns

**Bash hook with JSONL event log:**
```bash
# vibecraft-hook.sh
echo "$EVENT_JSON" >> ~/.vibecraft/data/events.jsonl
# optionally: curl -s -X POST "$VIBECRAFT_WS_NOTIFY" -d "$EVENT_JSON" &
```
- Events appended as JSONL to disk — persistent history
- Optional async WebSocket notification (non-blocking, `&` background)
- Server reads the JSONL file for history on client connect

**Complete event type set (9 hook events):**
- `pre_tool_use`, `post_tool_use`, `stop`, `subagent_stop`
- `session_start`, `session_end`, `user_prompt_submit`
- `notification`, `pre_compact`

The `notification` event captures **permission prompts** (among other things):
```typescript
type NotificationEvent = {
  type: 'notification'
  notification_type: 'permission_request' | 'idle' | 'auth_success' | 'elicitation'
  ...
}
```

**WebSocket protocol message types:**
- Server → client: `connected`, `event`, `history`, `sessions`, `tokens`, `permission_prompt`, `text_tiles`
- Client → server: `subscribe`, `history`, `ping`, `voice_control`, **`permission_response`**

**Spawning with `--permission-mode=bypassPermissions`:**
```typescript
execFile('tmux', [
  'new-session', '-d', '-s', tmuxSession,
  '-c', cwd,
  `PATH=${EXEC_PATH} claude -c --permission-mode=bypassPermissions...`
], ...)
```
- Vibecraft spawns agents with bypass mode for orchestrated multi-agent workflows
- Interactive sessions use normal permission flow

**Session status states:** `idle`, `working`, `waiting`, `offline`

**Session linking:** Claude session_id → managed session UUID via `claudeToManagedMap`

**CORS/origin validation:**
```typescript
function isOriginAllowed(origin: string | undefined): boolean {
  const url = new URL(origin)
  return url.hostname === 'localhost' || url.hostname === '127.0.0.1'
    || (url.hostname === 'vibecraft.sh' && url.protocol === 'https:')
}
```

### Lessons for rAI3DS

- **JSONL event log** enables history replay on reconnect — the 3DS could show recent activity on connect
- **`notification` event type** is the structured way to get permission prompts without scraping
- **`permission_response`** as a first-class WebSocket message type is the right pattern
- Multi-agent `bypassPermissions` mode for automated sub-tasks is powerful
- **Voice input** via Deepgram is a natural extension (the 3DS mic could feed this)
- Token count tracking from tmux output polling (separate from hooks)

---

## T3 Code

**Repo:** `pingdotgg/t3code`
**Concept:** Open-source desktop GUI for AI coding agents (Codex-first, Claude Code support added).

### Architecture

Monorepo: `apps/desktop`, `apps/web`, `apps/server`, `apps/marketing`

```
apps/server/src/
├── orchestration/
│   ├── decider.ts      ← command-to-event translator
│   ├── projector.ts    ← event-to-state projector (event sourcing)
│   ├── Schemas.ts      ← schema aliases from @t3tools/contracts
│   └── Layers/, Services/
├── wsServer/
│   └── pushBus.ts      ← typed pub/sub WebSocket bus
├── terminal/           ← terminal integration
├── persistence/        ← event store
├── git/               ← git integration
└── provider/          ← agent provider implementations
```

### Key Design Patterns

**Event sourcing architecture:**
```typescript
// decider.ts — commands → events
case "thread.turn.start": {
  return [userMessageEvent, turnStartRequestedEvent]
}

// projector.ts — events → read model
export function projectEvent(
  model: OrchestrationReadModel,
  event: OrchestrationEvent,
): Effect.Effect<OrchestrationReadModel, ...>
```
- All state derived from immutable events
- `MAX_THREAD_MESSAGES = 2000`, `MAX_THREAD_CHECKPOINTS = 500`
- Timeline revert support (undoing turns)

**Effect library for async:**
```typescript
// pushBus.ts
Queue.unbounded<PushJob>() → Effect.forever → send() → WebSocket.send()
```
- Fiber-based concurrent message queue
- Typed channels for type-safe WebSocket messages

**Permission/approval model:**
```typescript
// Commands include:
"ThreadApprovalResponseRequestedPayload"  // Claude requests approval
"thread.turn.interrupt"                    // Cancel a turn
"thread.turn.start"                        // Start with proposed plan
```

**Proposed plan pattern:**
- Before starting a turn, T3Code can receive a "proposed plan" from another thread
- This is multi-agent: one agent proposes, another approves/modifies before execution
- Sophisticated workflow beyond simple yes/no

### Lessons for rAI3DS

- Event sourcing is overkill for rAI3DS's current scale, but the **command/event separation** is a great pattern
- **`pushBus.ts`** typed WebSocket pub/sub is cleaner than the current `broadcast()` function
- The **proposed plan pattern** is interesting: show user what Claude plans to do before running
- T3Code's separation between `interactive` and `agent` modes maps to rAI3DS's "default session" vs "spawned session"

---

## Agent Deck

**Repo:** `asheshgoplani/agent-deck`
**Concept:** Terminal session manager TUI for AI coding agents (Claude, Gemini, OpenCode, Codex).

- Single TUI that multiplexes multiple agent sessions
- Native terminal UI — no browser needed
- Interesting as comparison: rAI3DS is the "hardware TUI" equivalent

---

## Summary Table

| Project | Transport | Approval Flow | Session Mgmt | Persistence |
|---------|-----------|--------------|--------------|-------------|
| ClawdGotchi | Unix socket | Observation only (no control) | Electron/IPC | None |
| Vibecraft | WebSocket | `permission_response` message | tmux + REST | JSONL event log |
| T3Code | WebSocket (typed) | Event-sourced approval commands | Terminal integration | Full event store |
| **rAI3DS current** | WebSocket | tmux send-keys (positional) | tmux + hooks | None |
| **rAI3DS ideal** | WebSocket | Hook decision response OR Channels | tmux or SDK | Optional JSONL |
