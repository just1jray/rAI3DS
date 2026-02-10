# Pokemon-Style Multi-Agent Creature System for 3DS

## Context

The 3DS companion currently has hardcoded agent tabs ("Claude", "Codex", "Gemini", "Cursor") that waste screen space and don't reflect actual sessions. We're replacing this with a Pokemon-inspired system: pixel art creatures represent agents, users "send out" new agents with a pokeball animation, and each creature animates based on its state. This transforms the 3DS from a single-agent approval device into a multi-session orchestrator.

Inspired by [clawdgotchi](https://github.com/stevysmith/clawdgotchi) (bouncing pixel crabs per session) and [vibecraft](https://github.com/nearcyan/vibecraft) (multi-session tmux management).

---

## Phase 0: Fix Scraper Showing Wrong Commands (CRITICAL — do first)

**Problem**: The scraper reads the FULL tmux pane, then walks backward from "Do you want to proceed?" looking for `╭` box-drawing characters. When Claude Code renders without box characters (or tmux strips them), the backward walk runs all the way to line 0 and collects the ENTIRE pane as "tool detail". The 3DS then shows whatever's at the top of the terminal, not the actual command.

**Root cause**: `companion-server/src/scraper.ts` line 19 — `tmux capture-pane -p` captures the full visible pane. Line 70 — backward `for` loop has no distance limit.

### Fix 1: Capture only the bottom of the pane (`scraper.ts` line 19)

```typescript
// Before:
const result = await $`tmux capture-pane -p -t ${TMUX_SESSION}`.quiet();

// After — only capture last 30 lines (prompt is always at bottom):
const result = await $`tmux capture-pane -p -t ${TMUX_SESSION} -S -30`.quiet();
```

The `-S -30` flag tells tmux to start capture 30 lines from the bottom. The permission prompt + its box is never more than ~15 lines. This is the single most impactful change.

### Fix 2: Limit backward search distance (`scraper.ts` line 70)

```typescript
// Before:
for (let i = promptIdx - 1; i >= 0; i--) {

// After — never search more than 15 lines above the prompt:
const searchLimit = Math.max(0, promptIdx - 15);
for (let i = promptIdx - 1; i >= searchLimit; i--) {
```

Even within the 30-line window, this prevents collecting unrelated text if the prompt is near the bottom and other content is above it.

### Fix 3: Graceful fallback when no box found (`scraper.ts`, after line 92)

If the backward walk never finds a `╭` (boxTopIdx stays -1), the collected boxLines may contain non-prompt text. Add a guard:

```typescript
// If no box border found, the text is unreliable — use only the
// 3 lines directly above the prompt (which are typically: tool type,
// tool detail, description in Claude Code's non-box format)
if (boxTopIdx === -1 && boxLines.length > 3) {
  // Keep only the last 3 entries (closest to the prompt)
  boxLines.splice(0, boxLines.length - 3);
}
```

### Why this fully fixes it

- **Hook data available** (normal case): hook provides correct text, scraper only triggers state transition. Already implemented in previous fix.
- **Hook data NOT available** (server restart, hook failure): scraper now captures only the bottom 30 lines, limits backward search to 15 lines, and falls back to the 3 lines closest to the prompt. The command is always in those lines.

### Files modified

| File | Change |
|------|--------|
| `companion-server/src/scraper.ts` | `-S -30` flag, search limit, graceful fallback |

---

## Phase 1: Animation Engine & Creature Renderer (3DS-side)

### New files: `3ds-app/source/creature.h` and `creature.c`

**Pixel art data format** — 16x16 grid, `u32` colors (0 = transparent):

```c
#define CREATURE_W 16
#define CREATURE_H 16

typedef struct {
    u32 pixels[CREATURE_H][CREATURE_W];
} CreatureFrame;
```

**Clawd design** (Catppuccin Peach `#fab387` body, based on Claude's TUI crab `#D77757`):
- Rows 0-2: Antenna/ear nubs
- Rows 3-9: Rectangular body with dark eye cutouts
- Rows 5-6: Arm nubs on sides
- Rows 10-15: Four legs in two pairs with feet

**Renderer**: `draw_creature(float x, float y, int scale, const CreatureFrame* frame)` — nested loop, skip transparent pixels, call `C2D_DrawRectSolid()` per pixel. Max 256 draw calls per creature.

### New files: `3ds-app/source/animation.h` and `animation.c`

**Tick-based system** running at 60fps:

```c
typedef struct {
    const CreatureFrame* frames;
    int frame_count;
    int ticks_per_frame;   // e.g. 20 ticks = ~3Hz
    bool one_shot;         // for spawn animation
} AnimDef;

typedef struct {
    const AnimDef* current;
    int frame_index;
    int tick_counter;
    bool finished;         // true when one_shot completes
} AnimState;
```

`anim_tick(AnimState*)` — called once per frame in main loop. Decrements counter, advances frame, loops or stops.

**Three animation states per creature** (2 frames each):
- **Idle**: Normal + raised 1px (gentle bob, ~3Hz)
- **Working**: Normal + brightened colors (pulse, ~6Hz)
- **Waiting**: Normal + yellow tint (urgent flash, ~7.5Hz)

**Pokeball spawn** — one-shot animation (~90 frames / 1.5s):
1. Frames 0-15: Circle grows (red top / white bottom, made of rects)
2. Frames 16-25: Circle splits open
3. Frames 26-35: White flash fills slot
4. Frames 36-55: Creature materializes (colors lerp from white)
5. Frames 56-90: Creature settles into idle bob

### Modified: `3ds-app/source/main.c`

- Add `anim_tick()` calls in main loop (once per creature per frame)
- Map `agent->state` to animation: IDLE→idle, WORKING→working, WAITING→waiting

---

## Phase 2: Bottom Screen Redesign (3DS-side)

### Modified: `3ds-app/source/ui.c`

Replace hardcoded tabs with party lineup. Layout adapts based on state:

**Idle mode** (no prompt pending, 320x240):
```
y=0-70:    Party lineup — 4 creature slots
           75px wide each, 5px gaps, creatures at scale 3 (48x48px)
           Name label below each creature
           Selected: Mauve border | Empty: dashed border + "+"
y=75-195:  Selected creature showcase
           Large creature (scale 5, 80x80px) centered
           State pill, context bar, current tool info
y=200-240: Status bar — auto-edit toggle + spawn button
```

**Prompt mode** (selected agent STATE_WAITING):
```
y=0-55:    Party lineup — compact, creatures at scale 2
           Selected creature plays waiting flash animation
y=58-120:  Tool detail card (type + scrollable command, 3 lines)
y=123-188: Action buttons (YES/ALWAYS/NO, 65px tall, same touch targets)
y=192-240: Auto-edit toggle + status
```

**New touch zones:**
- `ui_touch_creature_slot(touch)` → returns slot 0-3 or -1
- `ui_touch_spawn(touch)` → hit-test for "+" empty slot or spawn button

**New input in `main.c`:**
- L/R bumpers: cycle selected agent
- Tap creature slot: select that agent
- Tap empty "+" slot: trigger spawn (send message to server)

### Modified: `3ds-app/source/protocol.h`

Add spawn-related fields:

```c
typedef struct {
    // ... existing fields ...
    bool spawning;              // true during pokeball animation
    int spawn_anim_frame;       // animation progress
} Agent;
```

Add new message type for spawn requests/results.

---

## Phase 3: Multi-Session Server Architecture

### New file: `companion-server/src/session.ts`

**SessionManager** (pattern from clawdgotchi):

```typescript
interface ManagedSession {
  slot: number;              // 0-3, maps to 3DS party position
  claudeSessionId: string | null;  // set lazily on first hook event
  tmuxPaneId: string;        // tmux pane identifier
  status: 'spawning' | 'active' | 'idle' | 'ending';
  lastActivity: number;
}

const sessions = new Map<number, ManagedSession>();       // slot → session
const sessionIdMap = new Map<string, number>();            // claudeSessionId → slot
```

- `spawnSession(slot)`: Create tmux pane, launch `claude`, set status='spawning'
- `linkSession(claudeSessionId, slot)`: Called on first hook event from new session
- `killSession(slot)`: Kill tmux pane, clean up maps
- `getSlotForSessionId(id)`: Lookup for hook routing
- Health check: every 30s, poll `tmux list-panes`, mark dead sessions

**Lazy linking** (from vibecraft pattern): When a hook event arrives with an unknown `session_id`, check if there's a session in 'spawning' state and auto-link it.

### Modified: `companion-server/src/server.ts`

- Replace single `agentState` with `agentStates: AgentStatus[]` (4 slots)
- Replace single `pendingToolData` with per-slot map
- Replace single `claudeAdapter` with per-slot adapters
- Hook endpoints route by `session_id` → slot via SessionManager
- New endpoints:
  - Hook: `/hook/session-start` — captures session_id, links to slot
  - Hook: `/hook/session-end` — marks session ending
  - Hook: `/hook/stop` — marks session idle
  - Hook: `/hook/user-prompt` — marks session active
- WebSocket messages gain `slot` field
- New WS message types: `spawn_request`, `spawn_result`

### Modified: `companion-server/src/hooks.ts`

Expand hook subscriptions beyond PreToolUse/PostToolUse:

```typescript
const HOOK_EVENTS = [
  'SessionStart',       // → /hook/session-start
  'SessionEnd',         // → /hook/session-end
  'PreToolUse',         // → /hook/pre-tool (existing)
  'PostToolUse',        // → /hook/post-tool (existing)
  'UserPromptSubmit',   // → /hook/user-prompt
  'Stop',               // → /hook/stop
];
```

Use `matcher: ""` for PreToolUse/PostToolUse (existing pattern), no matcher for lifecycle events.

### Modified: `companion-server/src/index.ts`

- Import SessionManager, initialize on startup
- Scraper: one instance per active tmux pane (or single scraper cycling panes)
- On spawn request from 3DS: call `sessionManager.spawnSession(slot)`
- On spawn result: broadcast to 3DS with success/fail

### Modified: `companion-server/src/scraper.ts`

- Accept pane ID parameter to target specific tmux pane
- Support multiple scraper instances (one per session)

---

## Phase 4: Integration & Polish

### Wiring
- 3DS spawn button → WS `spawn_request` → server spawns tmux pane → `spawn_result` → 3DS plays pokeball animation
- Server hook events → routed to correct slot → WS broadcast with slot → 3DS updates correct creature
- 3DS action buttons → WS `action` with slot → server routes keystroke to correct adapter

### Top screen adaptation (`ui.c`)
- Single-agent expanded view: show large creature in activity card area
- Multi-agent compact view: show small creature next to each agent name

### Audio notification for approval prompts

**New file: `3ds-app/source/audio.h` and `audio.c`**

Play a short beep when any agent enters STATE_WAITING (permission prompt appeared). Uses CSND service — already part of `-lctru`, no new Makefile dependencies.

```c
// audio.h
void audio_init(void);
void audio_exit(void);
void audio_play_prompt_beep(void);  // short 880Hz tone, ~150ms
```

**Implementation** (`audio.c`):
- `audio_init()`: call `csndInit()`, pre-generate a 150ms 880Hz (A5) sine wave into a `linearAlloc()` buffer with fade-out envelope to avoid click
- `audio_play_prompt_beep()`: fire `csndPlaySound()` on the pre-allocated buffer (non-blocking, no sleep needed since buffer persists)
- `audio_exit()`: `linearFree()` the buffer, call `csndExit()`

**Trigger** in `main.c`: when state transitions to STATE_WAITING (detected by comparing previous frame's state), call `audio_play_prompt_beep()`. Only fires once per prompt appearance, not every frame.

```c
// In main loop, after network_poll():
if (agent->state == STATE_WAITING && prev_state != STATE_WAITING) {
    audio_play_prompt_beep();
}
prev_state = agent->state;
```

**Key details:**
- Buffer allocated with `linearAlloc()` (physically contiguous memory required by CSND)
- Pre-generate once at init, reuse on each beep — no per-beep allocation
- CSND channel 8, `SOUND_FORMAT_16BIT | SOUND_LOOP_DISABLE`, 22050 Hz sample rate
- No DSP firmware dump required (unlike NDSP), works on any 3DS with homebrew

### Failsafes (from vibecraft patterns)
- Working timeout: 2min, reset stuck 'working' status
- tmux health check: 30s interval, mark dead panes
- Graceful unknown session handling: auto-create if event arrives for unknown id

---

## Files Summary

| File | Status | Changes |
|------|--------|---------|
| `3ds-app/source/creature.h` | **New** | CreatureFrame struct, clawd pixel art arrays, draw function |
| `3ds-app/source/creature.c` | **New** | Pixel art data, `draw_creature()` renderer |
| `3ds-app/source/animation.h` | **New** | AnimDef, AnimState structs, tick function |
| `3ds-app/source/animation.c` | **New** | `anim_tick()`, pokeball spawn sequence |
| `3ds-app/source/audio.h` | **New** | `audio_init/exit/play_prompt_beep` declarations |
| `3ds-app/source/audio.c` | **New** | CSND beep: pre-gen sine wave buffer, play on prompt |
| `3ds-app/source/ui.c` | **Modified** | Party lineup, adaptive layout, creature rendering, spawn button |
| `3ds-app/source/ui.h` | **Modified** | New touch zone functions |
| `3ds-app/source/main.c` | **Modified** | Animation ticks, L/R agent cycling, spawn input |
| `3ds-app/source/protocol.h` | **Modified** | Spawn fields, new message types |
| `3ds-app/source/network.c` | **Modified** | Spawn request/result messages |
| `companion-server/src/session.ts` | **New** | SessionManager, spawn/kill, lazy linking |
| `companion-server/src/server.ts` | **Modified** | Multi-slot state, hook routing, spawn endpoints |
| `companion-server/src/hooks.ts` | **Modified** | Expanded hook event subscriptions |
| `companion-server/src/index.ts` | **Modified** | SessionManager init, multi-scraper |
| `companion-server/src/scraper.ts` | **Modified** | Per-pane targeting |
| `companion-server/src/types.ts` | **Modified** | New message types, session fields |

## Verification

### Phase 1
1. `docker compose run --rm 3ds-build` — compiles with new creature/animation files
2. Creature renders on screen at correct position with idle animation

### Phase 2
1. Bottom screen shows party lineup instead of hardcoded tabs
2. Touch/button selection highlights different slots
3. Layout snaps between idle and prompt modes
4. Pokeball animation plays visually (no actual spawn yet)

### Phase 3
1. `bun run companion-server/src/index.ts install` — hooks install for all event types
2. Spawn a session: server creates tmux pane, `claude` starts
3. Hook events route to correct slot via session_id
4. Kill a session: tmux pane dies, state cleaned up

### Phase 4 (end-to-end)
1. On 3DS: tap "+" slot → pokeball animation → creature appears
2. Creature animates based on actual Claude session state
3. When Claude needs approval → creature flashes → buttons appear → approve from 3DS
4. Multiple creatures independently tracking their sessions
5. Kill a session → creature fades / "return" animation
