# Plan: Migrate rAI3DS to `--sdk-url` WebSocket Protocol

## Context

The companion server currently uses **tmux scraping + HTTP hooks + keystroke injection** to interface with Claude Code. This is fragile (300ms polling, box-drawing char parsing, race conditions between scraper and keystrokes).

Claude Code has a hidden `--sdk-url` flag that makes it connect as a WebSocket client, sending structured NDJSON messages. [The-Vibe-Company/companion](https://github.com/The-Vibe-Company/companion) reverse-engineered this protocol. Adopting it gives us real-time structured data, reliable approve/deny, and no tmux dependency.

**The 3DS app and its protocol stay unchanged.** The server translates between NDJSON (CLI) and JSON (3DS).

## Architecture Change

```
Before:  3DS <--WS--> Server --tmux scrape/keystroke--> Claude Code (terminal)
After:   3DS <--WS--> Server <--NDJSON/WS-- Claude Code (--sdk-url subprocess)
```

## Scope Decisions

| Feature | In Scope (v1) | Deferred | Notes |
|---------|:---:|:---:|-------|
| Core NDJSON message handling | x | | system/init, assistant, result, control_request, tool_progress, system/status |
| Permission flow (yes/no/always) | x | | With correct `updatedInput` + `permission_suggestions` passthrough |
| Interrupt flow | x | | control_request { interrupt } |
| Auto-edit | x | | Reuse existing pattern matching |
| Multi-slot subprocess spawn | x | | Bun.spawn per slot |
| Context percent from usage | x | | Hardcode 200k initially, update from `result.modelUsage` |
| CLI reconnection with replay | | x | Requires server-side message buffer; CLI reconnects 3x then exits |
| `initialize` control_request | | x | Not needed unless we want custom system prompts or MCP |
| `stream_event` token streaming | | x | Requires `--verbose`; silently discard for now |
| Session persistence / `--resume` | | x | Step 7 (optional) |
| Authentication on WS server | | x | Local network only for now |

---

## SDK Protocol Reference

> Derived from [The-Vibe-Company/companion](https://github.com/The-Vibe-Company/companion) source code (ws-bridge.ts, types).

### Transport

Each message is a **single JSON object terminated by `\n`**. Multiple messages can arrive in a single WebSocket frame separated by newlines. Both sending and receiving must handle this:

```typescript
// Receiving: split by newline, parse each line independently
const lines = data.toString().split("\n").filter(l => l.trim());
for (const line of lines) {
  try {
    const msg = JSON.parse(line);
    dispatch(msg);
  } catch (e) {
    console.error("[cli] bad NDJSON line:", line);
  }
}

// Sending: always append \n
ws.send(JSON.stringify(payload) + "\n");
```

### Spawn Command

```bash
claude --sdk-url ws://localhost:3333/ws/cli/{slot} \
       --print \
       --output-format stream-json \
       --input-format stream-json \
       --verbose \
       -p ""
```

**Gotchas:**
- `-p ""` is **required** but its content is **ignored** in `--sdk-url` mode. The CLI waits for a `user` message over WebSocket instead.
- Both `--output-format` and `--input-format` **must** be `stream-json` or the CLI exits with an error.
- `--verbose` enables `stream_event` messages (token streaming). We include it for future use but silently discard those messages in v1.

### Message Schemas (CLI → Server)

#### `system/init` — First message after WS connect

```typescript
{
  type: "system",
  subtype: "init",
  session_id: string,       // CLI's internal session ID (for --resume)
  model: string,            // e.g. "claude-sonnet-4-5-20250929"
  cwd: string,              // working directory
  tools: string[],          // available tool names
  permissionMode: string,   // "default", "acceptEdits", "bypassPermissions"
  claude_code_version: string,
  uuid: string
  // also: mcp_servers, apiKeySource, slash_commands, agents, skills, plugins, output_style
}
```

#### `assistant` — Full LLM response

```typescript
{
  type: "assistant",
  message: {
    id: string,
    role: "assistant",
    model: string,
    content: ContentBlock[],  // text, tool_use, tool_result, thinking blocks
    stop_reason: string | null,
    usage: {
      input_tokens: number,
      output_tokens: number,
      cache_creation_input_tokens: number,
      cache_read_input_tokens: number
    }
  },
  session_id: string,
  error?: "authentication_failed" | "billing_error" | "rate_limit" | "server_error",
  uuid: string
}
```

**Important:** The `error` field on `assistant` messages indicates API-level errors (rate limit, billing, auth). Must check this and broadcast error state to 3DS.

#### `control_request` — Permission prompt

```typescript
{
  type: "control_request",
  request_id: string,        // UUID — correlate with control_response
  request: {
    subtype: "can_use_tool",
    tool_name: string,       // "Bash", "Edit", "Write", "Read", etc.
    input: Record<string, unknown>,
    tool_use_id: string,
    description?: string,
    permission_suggestions?: PermissionUpdate[]  // CLI's recommended rules for "always"
  }
}
```

#### `result` — Query complete

```typescript
{
  type: "result",
  subtype: "success" | "error_during_execution" | "error_max_turns" | "error_max_budget_usd",
  is_error: boolean,
  result: string,
  duration_ms: number,
  num_turns: number,
  total_cost_usd: number,
  usage: { input_tokens, output_tokens, cache_creation_input_tokens, cache_read_input_tokens },
  modelUsage: Record<string, {
    inputTokens: number,
    outputTokens: number,
    contextWindow: number,   // <-- actual context window for this model
    maxOutputTokens: number,
    costUSD: number
  }>,
  session_id: string,
  uuid: string
}
```

#### `tool_progress` — Tool execution heartbeat

```typescript
{
  type: "tool_progress",
  tool_name: string,
  tool_use_id: string,
  elapsed_time_seconds: number,
  uuid: string
}
```

#### `system/status` — Compacting or status change

```typescript
{
  type: "system",
  subtype: "status",
  status: "compacting" | null   // null = compacting ended
}
```

#### Other (silently discard in v1)

- `keep_alive` — consume silently, no response needed
- `stream_event` — token streaming (from `--verbose`), discard
- `tool_use_summary` — post-tool summary, discard
- `auth_status` — log errors, broadcast error state if `error` field present

### Message Schemas (Server → CLI)

#### `user` — Send prompt

```typescript
{
  type: "user",
  message: {
    role: "user",
    content: string           // text prompt from 3DS user
  },
  parent_tool_use_id: null,   // null for top-level
  session_id: string          // "" for first message, then CLI's session_id
}
```

#### `control_response` — Allow/deny tool

```typescript
// Allow
{
  type: "control_response",
  response: {
    subtype: "success",
    request_id: string,       // MUST match the control_request.request_id
    response: {
      behavior: "allow",
      updatedInput: Record<string, unknown>,     // REQUIRED — echo back request.input
      updatedPermissions?: PermissionUpdate[]    // for "always" action
    }
  }
}

// Deny
{
  type: "control_response",
  response: {
    subtype: "success",
    request_id: string,
    response: {
      behavior: "deny",
      message: "Denied by user via rAI3DS"
    }
  }
}
```

**Critical:** `updatedInput` is **required** in allow responses. Omitting it causes the CLI to use empty input, breaking the tool execution.

#### `control_request` — Interrupt (Server → CLI)

```typescript
{
  type: "control_request",
  request_id: crypto.randomUUID(),
  request: { subtype: "interrupt" }
}
```

### Permission Update Format (for "always")

When the user presses "always" (X button), use the CLI's own `permission_suggestions` if present. Otherwise construct:

```typescript
{
  type: "addRules",
  rules: [{ toolName: request.tool_name }],  // e.g. "Bash"
  behavior: "allow",
  destination: "session"  // ephemeral — lasts this session only
}
```

---

## Implementation Steps

### Step 1: Update types.ts

**File:** `companion-server/src/types.ts`

**Remove** hook types: `PreToolHook`, `PostToolHook`, `SessionStartHook`, `SessionEndHook`, `StopHook`, `UserPromptHook`

**Keep unchanged** all 3DS protocol types: `AgentState`, `AgentStatus`, `AgentStatusMessage`, `SpawnResultMessage`, `DSMessage` union

**Add** SDK protocol types:

```typescript
// --- SDK Incoming (CLI → Server) ---

export interface SDKSystemInit {
  type: "system";
  subtype: "init";
  session_id: string;
  model: string;
  cwd: string;
  tools: string[];
  permissionMode: string;
  claude_code_version: string;
  uuid: string;
}

export interface SDKAssistantMessage {
  type: "assistant";
  message: {
    id: string;
    role: "assistant";
    model: string;
    content: SDKContentBlock[];
    stop_reason: string | null;
    usage: SDKUsage;
  };
  session_id: string;
  error?: string;
  uuid: string;
}

export interface SDKContentBlock {
  type: "text" | "tool_use" | "tool_result" | "thinking";
  text?: string;
  id?: string;
  name?: string;
  input?: Record<string, unknown>;
  tool_use_id?: string;
  content?: string | SDKContentBlock[];
  is_error?: boolean;
  thinking?: string;
}

export interface SDKUsage {
  input_tokens: number;
  output_tokens: number;
  cache_creation_input_tokens: number;
  cache_read_input_tokens: number;
}

export interface SDKControlRequest {
  type: "control_request";
  request_id: string;
  request: {
    subtype: "can_use_tool";
    tool_name: string;
    input: Record<string, unknown>;
    tool_use_id: string;
    description?: string;
    permission_suggestions?: SDKPermissionUpdate[];
  };
}

export interface SDKPermissionUpdate {
  type: "addRules" | "replaceRules" | "removeRules" | "setMode";
  rules?: { toolName: string; ruleContent?: string }[];
  behavior?: "allow" | "deny" | "ask";
  destination: "session" | "projectSettings" | "userSettings" | "localSettings";
  mode?: string;
}

export interface SDKResult {
  type: "result";
  subtype: "success" | "error_during_execution" | "error_max_turns" | "error_max_budget_usd";
  is_error: boolean;
  result: string;
  duration_ms: number;
  num_turns: number;
  total_cost_usd: number;
  usage: SDKUsage;
  modelUsage?: Record<string, {
    inputTokens: number;
    outputTokens: number;
    contextWindow: number;
    maxOutputTokens: number;
    costUSD: number;
  }>;
  session_id: string;
  uuid: string;
}

export interface SDKToolProgress {
  type: "tool_progress";
  tool_name: string;
  tool_use_id: string;
  elapsed_time_seconds: number;
  uuid: string;
}

export interface SDKSystemStatus {
  type: "system";
  subtype: "status";
  status: "compacting" | null;
}

// --- SDK Outgoing (Server → CLI) ---

export interface SDKUserMessage {
  type: "user";
  message: { role: "user"; content: string };
  parent_tool_use_id: null;
  session_id: string;
}

export interface SDKControlResponseAllow {
  type: "control_response";
  response: {
    subtype: "success";
    request_id: string;
    response: {
      behavior: "allow";
      updatedInput: Record<string, unknown>;
      updatedPermissions?: SDKPermissionUpdate[];
    };
  };
}

export interface SDKControlResponseDeny {
  type: "control_response";
  response: {
    subtype: "success";
    request_id: string;
    response: {
      behavior: "deny";
      message: string;
    };
  };
}

export interface SDKInterruptRequest {
  type: "control_request";
  request_id: string;
  request: { subtype: "interrupt" };
}

// --- Internal state ---

export interface CLIConnection {
  ws: ServerWebSocket<WSData>;
  slot: number;
  sessionId: string | null;
  model: string | null;
  contextWindow: number;  // default 200_000, updated from result.modelUsage
}

export interface PendingPermission {
  requestId: string;
  slot: number;
  toolName: string;
  toolInput: Record<string, unknown>;
  toolUseId: string;
  description: string;
  permissionSuggestions?: SDKPermissionUpdate[];  // from CLI, for "always"
}

export interface WSData {
  type: "cli" | "3ds";
  slot?: number;
}
```

### Step 2: Create cli-handler.ts (new file)

**File:** `companion-server/src/cli-handler.ts`

Core NDJSON message handler. This is the heart of the migration.

**State:**
- `cliConnections: Map<number, CLIConnection>` — slot → WebSocket + sessionId + model + contextWindow
- `pendingPermissions: Map<string, PendingPermission>` — requestId → permission data

**Functions:**

#### `handleCLIOpen(ws, slot)`
- Register CLI connection in `cliConnections` with defaults: `sessionId: null`, `model: null`, `contextWindow: 200_000`

#### `handleCLIMessage(ws, data)`
**NDJSON parsing** — split by `\n`, parse each line independently in try/catch:
```typescript
const lines = data.toString().split("\n").filter((l: string) => l.trim());
for (const line of lines) {
  try {
    const msg = JSON.parse(line);
    dispatchCLIMessage(ws, msg);
  } catch (e) {
    console.error("[cli] bad NDJSON line:", line.slice(0, 100));
  }
}
```

**Dispatch by message type:**

| Message | Handler |
|---------|---------|
| `type: "system", subtype: "init"` | Store `sessionId`, `model` on CLIConnection. Call `updateState(slot, { state: "idle", active: true, name: model })`. |
| `type: "assistant"` | Check `error` field first — if present (rate_limit, billing, auth), broadcast error state. Otherwise: extract text summary from `content[0].text`, compute `contextPercent` from `usage.input_tokens / conn.contextWindow * 100`, call `updateState(slot, { state: "working", contextPercent, message: summary })`. |
| `type: "control_request"` | Extract `request.subtype`. If `can_use_tool`: extract tool detail (see below), check auto-edit patterns. If auto-edit match → auto-approve via `sendControlResponse` with `behavior: "allow"` and `updatedInput: request.input`. If no match → store in `pendingPermissions` (including `permission_suggestions`), call `updateState(slot, { state: "waiting", promptToolType: toolType, promptToolDetail: detail, promptDescription: description })`. |
| `type: "result"` | Update `contextWindow` from `modelUsage[model].contextWindow` if available. If `is_error && subtype === "error_during_execution"` → `updateState("error")`. If `is_error && subtype === "error_max_turns"` → `updateState("idle")` (normal completion, just hit limit). Otherwise → `updateState("idle")`. |
| `type: "tool_progress"` | `updateState(slot, { state: "working", message: "Running: ${tool_name} (${elapsed}s)" })` |
| `type: "system", subtype: "status"` | If `status === "compacting"` → `updateState(slot, { state: "working", message: "Compacting context..." })`. If `status === null` → `updateState(slot, { state: "working", message: "" })`. |
| `type: "keep_alive"` | No-op. Silently consume. |
| `type: "stream_event"` | No-op. Silently discard (v1). |
| `type: "tool_use_summary"` | No-op. Silently discard (v1). |
| `type: "auth_status"` | If `error` field present → `updateState(slot, { state: "error", message: error })`. Otherwise log. |

#### `handleCLIClose(ws, slot)`
- Remove from `cliConnections`
- Find and remove all `pendingPermissions` for this slot
- **Do NOT mark slot as done here** — WebSocket disconnect may be temporary. The process exit handler (`proc.exited.then()` in session.ts) is the authoritative death signal.
- Log: `[cli] slot ${slot} WebSocket disconnected`

#### `sendControlResponse(slot, requestId, response)`
- Look up CLI connection for slot. If none exists, log warning and return.
- Build `SDKControlResponseAllow` or `SDKControlResponseDeny` envelope.
- Send as NDJSON (`JSON.stringify(payload) + "\n"`).

#### `sendUserMessage(slot, content)`
- Look up CLI connection. If none exists, log warning and return.
- **Immediately** call `updateState(slot, { state: "working", message: "Processing prompt..." })` — don't wait for `assistant` message (prevents 1-5s idle gap).
- Build `SDKUserMessage` with `session_id: conn.sessionId ?? ""`.
- Send as NDJSON.

#### `sendInterrupt(slot)`
- Look up CLI connection. If none exists, return.
- Build `SDKInterruptRequest` with `request_id: crypto.randomUUID()`.
- Send as NDJSON.
- `updateState(slot, { state: "working", message: "Interrupting..." })`

#### `resolvePermission(slot, action)`
- Find the pending permission for this slot. If none exists (already resolved or cancelled), log and return (no-op — handles rapid button presses).
- Remove from `pendingPermissions`.
- Based on action:
  - `"yes"` → `sendControlResponse(slot, requestId, { behavior: "allow", updatedInput: original.input })`
  - `"always"` → Same as yes, plus `updatedPermissions`: use `permission_suggestions` from the stored `PendingPermission` if present, otherwise construct `{ type: "addRules", rules: [{ toolName }], behavior: "allow", destination: "session" }`
  - `"no"` → `sendControlResponse(slot, requestId, { behavior: "deny", message: "Denied by user via rAI3DS" })`
- `updateState(slot, { state: "working" })` after sending response.

#### `cancelPendingPermissions(slot)`
- Remove all pending permissions for this slot from the map.
- If the slot was in "waiting" state, broadcast state change to clear the 3DS prompt.

#### `getCliSessionId(slot)` — returns `sessionId` for future `--resume` support

#### `getCliConnection(slot)` — returns `CLIConnection | undefined`

**Tool detail extraction** (same logic as current pre-tool hook):
```typescript
function extractToolDetail(input: Record<string, unknown>): string {
  const keys = ["command", "file_path", "pattern", "query", "url"];
  for (const key of keys) {
    if (typeof input[key] === "string") return input[key] as string;
  }
  // Fallback: first string value
  for (const val of Object.values(input)) {
    if (typeof val === "string") return val;
  }
  return "";
}
```

### Step 3: Rewrite session.ts

**File:** `companion-server/src/session.ts`

Replace tmux session management with `Bun.spawn()` subprocess management.

**New `ManagedSession`:**
```typescript
interface ManagedSession {
  slot: number;
  cliSessionId: string | null;   // set from system/init
  process: Subprocess;
  pid: number;
  status: "spawning" | "active" | "idle" | "ending";
  lastActivity: number;
  resumeSessionId?: string;      // for --resume
}
```

**`sessions: Map<number, ManagedSession>`** — slot number → session

#### `spawnSession(slot, cwd?, resumeId?)`

```typescript
export function spawnSession(slot: number, cwd?: string, resumeId?: string): boolean {
  if (sessions.has(slot)) {
    console.error(`[session] slot ${slot} already occupied`);
    return false;
  }

  const args = [
    "claude",
    "--sdk-url", `ws://localhost:3333/ws/cli/${slot}`,
    "--print",
    "--output-format", "stream-json",
    "--input-format", "stream-json",
    "--verbose",
    "-p", ""
  ];
  if (resumeId) {
    args.push("--resume", resumeId);
  }

  try {
    const proc = Bun.spawn(args, {
      cwd: cwd ?? process.cwd(),
      stdout: "ignore",
      stderr: "pipe",     // capture for error diagnosis
    });

    const session: ManagedSession = {
      slot,
      cliSessionId: null,
      process: proc,
      pid: proc.pid,
      status: "spawning",
      lastActivity: Date.now(),
    };
    sessions.set(slot, session);

    // Monitor exit
    proc.exited.then((exitCode) => {
      console.log(`[session] slot ${slot} (PID ${proc.pid}) exited with code ${exitCode}`);
      sessions.delete(slot);               // Clean up — allows respawn of same slot
      cancelPendingPermissions(slot);       // Clear any dangling permission prompts
      updateState(slot, { state: "done", active: false });
    });

    return true;
  } catch (e) {
    console.error(`[session] failed to spawn slot ${slot}:`, e);
    return false;
  }
}
```

**Key design decisions:**
- `sessions.delete(slot)` on exit → allows respawning the same slot number (critical for slot 0 recovery)
- `stderr: "pipe"` → can read stderr for diagnosis on spawn failure
- `proc.exited.then()` is the **authoritative death signal**, not WebSocket close
- try/catch around `Bun.spawn()` handles binary-not-found errors

#### `killSession(slot)`
```typescript
export function killSession(slot: number): void {
  const session = sessions.get(slot);
  if (!session) return;
  sendInterrupt(slot);                    // graceful interrupt first
  session.process.kill();                 // then kill
  // proc.exited.then() handles cleanup
}
```

#### `healthCheck()`
- Iterate sessions, check `proc.exitCode !== null` for dead processes
- Dead processes should already be cleaned up by `proc.exited.then()`, but this is a safety net

#### `autoSpawnDefaultSession()`
- Calls `spawnSession(0)` after a short delay (100ms — gives Bun.serve time to be fully ready)
- On failure, logs error but does not crash server

**Remove:** all tmux references, `createClaudeAdapter`, `initDefaultSession`, `linkSession`, `resolveSlot`, `getAdapterForSlot`, `sessionIdMap`

### Step 4: Rewrite server.ts

**File:** `companion-server/src/server.ts`

**WebSocket routing** — dual path on same port:
- `/ws/cli/:slot` → CLI connections (NDJSON), dispatch to cli-handler
- Everything else → 3DS connections (JSON), existing protocol

#### `fetch()` handler

```typescript
fetch(req, server) {
  const url = new URL(req.url);

  // CLI WebSocket: /ws/cli/:slot
  const cliMatch = url.pathname.match(/^\/ws\/cli\/(\d+)$/);
  if (cliMatch) {
    const slot = parseInt(cliMatch[1]);
    if (slot < 0 || slot >= MAX_SLOTS) return new Response("Invalid slot", { status: 400 });
    const upgraded = server.upgrade(req, { data: { type: "cli", slot } as WSData });
    return upgraded ? undefined : new Response("Upgrade failed", { status: 500 });
  }

  // Health endpoint
  if (url.pathname === "/health" && req.method === "GET") {
    return Response.json({ agents: agentStates, autoEdit: autoEditEnabled, wsClients: wsClients.size });
  }

  // 3DS WebSocket: everything else
  const upgraded = server.upgrade(req, { data: { type: "3ds" } as WSData });
  return upgraded ? undefined : new Response("rAI3DS companion server", { status: 200 });
}
```

#### `websocket` handler

```typescript
websocket: {
  open(ws) {
    if (ws.data.type === "cli") {
      handleCLIOpen(ws, ws.data.slot!);
    } else {
      wsClients.add(ws);
      broadcastAllSlots();  // send current state to new 3DS client
    }
  },
  message(ws, data) {
    if (ws.data.type === "cli") {
      handleCLIMessage(ws, typeof data === "string" ? data : new TextDecoder().decode(data));
    } else {
      try {
        const msg = JSON.parse(typeof data === "string" ? data : new TextDecoder().decode(data));
        handleWsMessage(msg);
      } catch (e) {
        console.error("[ws] bad 3DS message:", e);
      }
    }
  },
  close(ws) {
    if (ws.data.type === "cli") {
      handleCLIClose(ws, ws.data.slot!);
    } else {
      wsClients.delete(ws);
    }
  }
}
```

**Note:** Binary frames from CLI are handled via `TextDecoder` (just in case, even though text frames are expected).

#### `handleWsMessage(msg)` rewrite

```typescript
function handleWsMessage(msg: DSMessage) {
  switch (msg.type) {
    case "action": {
      // Route by slot — 3DS currently hardcodes slot:0, but we respect it if sent
      const slot = msg.slot ?? 0;
      if (msg.action === "escape") {
        sendInterrupt(slot);
      } else {
        resolvePermission(slot, msg.action);  // "yes" | "always" | "no"
      }
      break;
    }
    case "command": {
      const slot = msg.slot ?? 0;
      if (msg.command === "spawn") {
        // Find first free slot
        const freeSlot = findFreeSlot();
        if (freeSlot === null) {
          broadcastSpawnResult(0, false, "No free slots");
          return;
        }
        const success = spawnSession(freeSlot);
        broadcastSpawnResult(freeSlot, success, success ? undefined : "Spawn failed");
      } else {
        sendUserMessage(slot, msg.command);
      }
      break;
    }
    case "spawn_request": {
      const slot = msg.slot;
      const success = spawnSession(slot);
      broadcastSpawnResult(slot, success, success ? undefined : "Spawn failed");
      break;
    }
    case "config": {
      if (msg.autoEdit !== undefined) {
        autoEditEnabled = msg.autoEdit;
        broadcastAllSlots();
      }
      break;
    }
  }
}
```

**Remove:** `pendingToolData` map, all hook payload parsing, imports from hooks.ts/scraper.ts/context.ts/adapters

**Keep unchanged:** `agentStates[]`, `wsClients`, `broadcastSlotState()`, `broadcastAllSlots()`, `broadcastSpawnResult()`, `updateState()`, `getAgentState()`, `isAutoEditEnabled()`, `MAX_SLOTS`

### Step 5: Simplify index.ts

**File:** `companion-server/src/index.ts`

**Before (current):**
```
1. Parse CLI command (start/install/uninstall/help)
2. initDefaultSession()        — tmux session
3. startServer()               — HTTP + WS
4. startContextTracker(10_000) — JSONL file polling
5. startScraper({callbacks})   — 300ms tmux polling
6. Health check interval (30s) — tmux has-session
```

**After:**
```
1. Parse CLI command (start/help only — no install/uninstall)
2. startServer()               — HTTP + WS
3. autoSpawnDefaultSession()   — Bun.spawn slot 0
4. Health check interval (30s) — checks subprocess PIDs
```

**Remove:**
- Imports: `installHooks`, `uninstallHooks`, `startContextTracker`, `startScraper`, `initDefaultSession`, `getAdapterForSlot`, `getPendingToolData`, `createClaudeAdapter`
- CLI commands: `install`, `uninstall`
- Scraper startup and all scraper callbacks
- Context tracker startup
- Auto-edit logic from scraper callback (moved to cli-handler)

**Add:**
- `autoSpawnDefaultSession()` call after `startServer()`
- Import from cli-handler and session

### Step 6: Delete obsolete files

| File | Reason |
|------|--------|
| `companion-server/src/scraper.ts` | Replaced by NDJSON `can_use_tool` messages |
| `companion-server/src/adapters/claude.ts` | Replaced by `control_response` messages |
| `companion-server/src/hooks.ts` | Replaced by SDK protocol (no more curl hooks) |
| `companion-server/src/context.ts` | Replaced by inline `usage` from `assistant` messages |

Also delete the `companion-server/src/adapters/` directory if empty after removing `claude.ts`.

### Step 7: Optional — Session persistence

**New file:** `companion-server/src/persistence.ts`

- Save `cliSessionId` per slot to `./raids-sessions.json` (debounced 2s writes)
- Persist after first `result` message (session is stable at that point)
- On server restart, load and pass to `spawnSession()` as `resumeId` for `--resume` flag
- If `--resume` spawn exits within 5 seconds (session file gone/corrupt), retry without `--resume`
- Low priority — can defer to a follow-up

---

## Message Translation Reference

| CLI → Server | 3DS Broadcast |
|---|---|
| `system/init` | `agent_status { state: "idle", active: true }` |
| `control_request { can_use_tool }` | `agent_status { state: "waiting", promptToolType, promptToolDetail, promptDescription }` |
| `assistant` (with usage) | `agent_status { state: "working", contextPercent }` |
| `assistant` (with `error` field) | `agent_status { state: "error", message: error }` |
| `result { success }` | `agent_status { state: "idle" }` |
| `result { error_during_execution }` | `agent_status { state: "error" }` |
| `result { error_max_turns }` | `agent_status { state: "idle" }` (normal completion, hit limit) |
| `tool_progress` | `agent_status { state: "working", message: "Running: tool (Ns)" }` |
| `system/status { compacting }` | `agent_status { state: "working", message: "Compacting context..." }` |
| `auth_status { error }` | `agent_status { state: "error", message: error }` |
| (sendUserMessage called) | `agent_status { state: "working", message: "Processing prompt..." }` |

| 3DS Action | CLI Response |
|---|---|
| `yes` | `control_response { behavior: "allow", updatedInput: original }` |
| `always` | `control_response { behavior: "allow", updatedInput, updatedPermissions: permission_suggestions \|\| addRules }` |
| `no` | `control_response { behavior: "deny", message: "Denied by user via rAI3DS" }` |
| `escape` | `control_request { interrupt }` |

## Known Limitations (v1)

1. **No CLI reconnection replay** — If the CLI WebSocket disconnects and reconnects, the server does not replay missed messages. The CLI will retry 3 times then exit. Process exit handler cleans up.

2. **3DS hardcodes slot 0** — The 3DS app sends `slot: 0` in all action/command messages. Multi-slot permission management requires a future 3DS update, or server-side routing by agent name. For v1, the server uses `msg.slot ?? 0`.

3. **No `initialize` control_request** — We skip sending this before the first user message. This means no custom system prompts, MCP servers, or hooks registered via the SDK. Can add later if needed.

4. **No token streaming to 3DS** — `stream_event` messages are discarded. Could provide partial response display in a future version.

## 3DS App: No Changes Required

The 3DS protocol (`agent_status`, `action`, `command`, `config`, `spawn_request`, `spawn_result`) is unchanged. The 3DS app doesn't need to know the server backend changed.

## Verification

1. **Build check:** `bun run src/index.ts --help` — no import errors
2. **Server startup:** `bun run src/index.ts` — starts on :3333, spawns slot 0 subprocess
3. **CLI connects:** Server logs `[cli] slot 0 connected` then `[cli] system/init: session_id=... model=...`
4. **Manual CLI test:** `claude --sdk-url ws://localhost:3333/ws/cli/0 --print --output-format stream-json --input-format stream-json --verbose -p ""` — should connect, server logs `system/init`
5. **Send prompt:** 3DS sends command → server shows "Processing prompt..." → assistant messages arrive → 3DS shows working with context % → result → idle
6. **Permission flow:** Trigger a tool → 3DS shows waiting with tool type/detail → press A → CLI continues
7. **Auto-edit:** Enable auto-edit → trigger Edit tool → auto-approved without 3DS prompt, server logs `[auto-edit]`
8. **Always:** Trigger tool → press X → subsequent same tool auto-approved for rest of session
9. **Deny:** Trigger tool → press B → CLI shows "Denied by user via rAI3DS"
10. **Interrupt:** Press escape on 3DS → CLI aborts turn → state returns to idle
11. **Multi-slot:** Spawn slot 1 from 3DS → second `claude` subprocess connects → independent operation
12. **Process death:** Kill subprocess → server detects via `proc.exited`, marks slot done → 3DS shows done
13. **Respawn after death:** Spawn same slot again → works (session entry cleaned up on death)
14. **Error handling:** Rate limit or auth error → 3DS shows error state
15. **3DS build:** `docker compose run --rm 3ds-build` — 3DS app builds unchanged, connects and works
