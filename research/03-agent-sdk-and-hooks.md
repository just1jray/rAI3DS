# Claude Agent SDK & Hooks System Deep Dive

*Researched: 2026-03-27*

## The Hooks System (Current + What We're Missing)

### Hook Types Available

Claude Code supports 4 hook implementations:

| Type | Description |
|------|-------------|
| `"command"` | Runs a shell command, JSON on stdin |
| `"http"` | POSTs JSON to an HTTP endpoint |
| `"prompt"` | Sends to Claude model for evaluation |
| `"agent"` | Spawns a subagent for complex validation |

**rAI3DS currently uses `"command"` type only** (curl calls). We should evaluate `"http"` type.

### Hook Events Available

| Event | When | Can Block? |
|-------|------|-----------|
| `PreToolUse` | Before tool executes | **YES** |
| `PostToolUse` | After tool completes | No |
| `Stop` | Agent stops | No |
| `SubagentStop` | Subagent stops | No |
| `SessionStart` | Session begins | No |
| `SessionEnd` | Session ends | No |
| `UserPromptSubmit` | User submits prompt | Yes (can modify) |
| `Notification` | Permission prompts, idle, auth | **YES** (for permission) |
| `PreCompact` | Before context compaction | No |

**rAI3DS registers:** PreToolUse, PostToolUse, Stop, UserPromptSubmit
**rAI3DS missing:** `Notification` (most important!), SessionStart, SessionEnd, SubagentStop, PreCompact

### The Critical Notification Event

The `Notification` hook is fired when Claude Code shows a permission prompt:

```json
{
  "hook_event_name": "Notification",
  "session_id": "abc123",
  "notification_type": "permission_request",
  "tool_name": "Bash",
  "tool_input": { "command": "rm -rf ./dist" },
  "cwd": "/home/user/project"
}
```

**This is the structured, scraping-free way to detect approval prompts.**

When this hook blocks (exit code 2, or returns JSON with decision), Claude Code waits.

### PreToolUse Blocking — The Right Way

The current code returns `{ action: "approve" }` immediately. Instead, a blocking `PreToolUse` hook can pause Claude until the 3DS responds:

**Exit code based (simple):**
```bash
# Exit 2 = block, Exit 0 = allow
exit 0   # allow
exit 2   # block (stderr message shown to user)
```

**JSON output based (rich):**
```json
{
  "hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "allow",   // or "deny" or "ask"
    "permissionDecisionReason": "Approved from 3DS"
  }
}
```

### HTTP Hook Type — Better Than curl

Instead of:
```bash
curl -s -X POST http://localhost:3333/hook/pre-tool -H "Content-Type: application/json" -d @-; exit 0
```

Use HTTP hooks directly in settings.json:
```json
{
  "type": "http",
  "url": "http://localhost:3333/hook/pre-tool",
  "timeout": 30,
  "statusMessage": "Waiting for 3DS approval..."
}
```

The HTTP response body is parsed as JSON and respected for decisions — no curl overhead, cleaner.

**But the real power:** HTTP hook requests **block Claude** until the server responds. If the server holds the HTTP connection open while waiting for the 3DS input, the entire permission flow becomes:

```
1. Claude wants to run Bash command
2. PreToolUse HTTP hook → POST to companion server (BLOCKS Claude)
3. Companion server broadcasts { state: "waiting", toolType, toolDetail } to 3DS
4. 3DS shows approval dialog
5. User presses Yes/No on 3DS
6. 3DS sends { type: "action", action: "yes" } over WebSocket
7. Companion server receives WebSocket message
8. Companion server responds to the BLOCKED HTTP request with:
   { "hookSpecificOutput": { "permissionDecision": "allow" } }
9. Claude proceeds or is denied
```

**This eliminates:**
- tmux screen scraping entirely
- `tmux send-keys` keystrokes
- The dual-source race condition
- Fragile prompt detection parsing

---

## Claude Agent SDK

**Package:** `@anthropic-ai/claude-agent-sdk`

### Core API

```typescript
import { query } from "@anthropic-ai/claude-agent-sdk";

for await (const message of query({
  prompt: "Fix the bug in auth.py",
  options: {
    allowedTools: ["Read", "Edit", "Bash"],
    canUseTool: async (toolName, input, context) => {
      // This fires instead of the interactive prompt
      // Return allow/deny programmatically
      return { permissionDecision: "allow" };
    }
  }
})) {
  console.log(message);
}
```

### `canUseTool` Callback — The SDK Approval Hook

```typescript
interface CanUseToolOptions {
  signal: AbortSignal;
  suggestions?: PermissionUpdate[];  // From permission rules
}

async function canUseTool(
  toolName: string,
  input: Record<string, unknown>,
  options: CanUseToolOptions
): Promise<{ permissionDecision: "allow" | "deny" } | undefined>
```

**This is the `canUseTool` callback** — fires whenever Claude needs permission. It **pauses execution** until the callback resolves.

For rAI3DS, we could:
1. Register a `canUseTool` callback in the SDK
2. When it fires, broadcast the approval request to connected 3DS clients
3. Hold a Promise that resolves when the 3DS responds
4. Return the decision

### Permission Evaluation Order (SDK)

1. **Hooks** (from settings.json, if `settingSources` specified) → allow/deny/continue
2. **Deny rules** (`disallowedTools`) → always block
3. **Permission mode** (bypassPermissions, acceptEdits, etc.)
4. **Allow rules** (`allowedTools`)
5. **`canUseTool` callback** → ask the user

### SDK vs tmux Approach

| Aspect | Current (tmux) | SDK approach |
|--------|---------------|-------------|
| Session type | Interactive Claude Code | Programmatic agent |
| Approval detection | Screen scraping | `canUseTool` callback |
| Approval sending | `tmux send-keys` | Return from callback |
| User can type in terminal | Yes | No (non-interactive) |
| Session history | Preserved in tmux | SDK session IDs |
| Attaches to existing sessions | Yes (pre-existing tmux) | No (spawns new) |
| Multi-turn conversation | Yes (interactive) | Yes (streaming) |

**Verdict:** The SDK is excellent for *spawned* sessions (user starts from 3DS). But for the primary use case — attaching to an existing `claude` session the developer already has open in their terminal — the SDK is not applicable. You can't wrap an existing interactive session with the SDK.

**Hybrid approach:** Use the SDK for 3DS-spawned sessions (slots 1–3) and use hooks+HTTP for the developer's primary session (slot 0).

---

## Claude Code Channels — The Game Changer

**Launched: March 20, 2026 (v2.1.80) — 7 days before this research**

### What Are Channels?

A channel is an **MCP server that runs as a subprocess of Claude Code**, communicating via stdio. Claude Code spawns it with `--channels`.

```bash
claude --channels mcp://localhost:9001/channel-server
```

Or in `.mcp.json` / settings with the channel server definition.

### Permission Relay Capability

Channel servers that declare the **`permission_relay`** capability receive permission prompts from Claude Code and can forward them anywhere:

```
Claude Code needs Bash approval
        ↓
Sends permission_request to channel server (MCP)
        ↓
Channel server forwards to rAI3DS companion server
        ↓
Companion server broadcasts to 3DS via WebSocket
        ↓
User approves/denies on 3DS
        ↓
Decision flows back: 3DS → companion → channel → Claude Code
```

**This is Anthropic's official mechanism for remote permission approval.**

### Security Model

- The channel MCP server runs on the same machine as Claude Code
- An allowlist controls which external senders can trigger permission decisions
- Only allowlisted senders can approve/deny tool use

### How to Implement

1. Write a channel MCP server (TypeScript/Python)
2. Declare `permission_relay` capability in MCP server manifest
3. The channel server connects to the companion server (or IS the companion server)
4. Install via `--channels` flag or settings.json

### Comparison to Current Approach

| Aspect | Current hooks+scraping | Channels |
|--------|----------------------|---------|
| Approval detection | tmux scraping (fragile) | MCP event (structured) |
| Approval sending | tmux send-keys | MCP response |
| Tool data richness | Partial (hook + scraper) | Complete (MCP input) |
| Official Anthropic support | Hooks yes, scraper no | Yes |
| Requires Claude version | Any | v2.1.80+ |
| Works with existing sessions | Yes (any tmux) | Only --channels enabled sessions |

**Key trade-off:** Channels require Claude Code to be started with `--channels` (or configured in settings). If the developer just runs `claude` without it, channels don't activate.

**Mitigation:** Write a wrapper script that the user runs instead:
```bash
#!/bin/bash
# raids — start claude with rAI3DS channel
claude --channels ./companion-server/channel-server.mcp.js "$@"
```

Or add to global Claude settings so it always activates.

---

## Recommended Hook Architecture

### Phase 1: HTTP hooks + blocking approval (drop scraping)

Change `hooks.ts` to install `type: "http"` hooks and hold HTTP responses until 3DS decides:

```typescript
// server.ts — pre-tool handler becomes long-poll
if (path === "/hook/pre-tool" && req.method === "POST") {
  const body = await req.json() as PreToolHook
  const slot = resolveSlot(body.session_id)

  // Broadcast waiting state to 3DS
  updateState(slot, { state: "waiting", promptToolType: body.tool_name, ... })

  // Hold HTTP connection — wait for 3DS decision
  const decision = await waitForDecision(slot, timeout=25_000)

  // Respond with permission decision
  return Response.json({
    hookSpecificOutput: {
      hookEventName: "PreToolUse",
      permissionDecision: decision,  // "allow" | "deny"
    }
  })
}
```

Also register the `Notification` hook to catch any prompts that slip through.

### Phase 2: Add Channels support (alongside Phase 1)

Implement the channel MCP server as an optional enhancement. Users who want to use rAI3DS with a wrapper script get cleaner permission flow; users who run plain `claude` fall back to HTTP hooks.

### Phase 3: SDK for spawned sessions

When the 3DS requests a new agent (slot 1–3), spawn it using the Agent SDK rather than raw `tmux new-session claude`. This gives proper `canUseTool` integration for those sessions.
