# Design Recommendations

*Researched: 2026-03-27*

## Summary Verdict

**Should the design stay as-is?** Mostly yes — the fundamental architecture is sound. The WebSocket transport, hooks system, and tmux session management are all correct choices. Specific components need to change.

**Should it convert to the Agent SDK?** Partially. Use the SDK for *spawned* sessions (slots 1–3), not for the primary developer session (slot 0). The SDK cannot wrap an existing interactive tmux session.

**Should it convert to SSH?** No. SSH is the wrong paradigm for a structured game-controller UI. It adds massive complexity for no architectural benefit.

**Should it add Claude Code Channels?** Yes — this is the most impactful near-term improvement. Channels (v2.1.80+) are Anthropic's official mechanism for remote permission relay. Implementing a channel server is the right path forward.

---

## Critical Issue: The Scraper Must Die

The `scraper.ts` tmux screen scraper is the biggest technical debt in the project. It is fragile, approximate, and will break as Claude Code evolves.

The solution is a **blocking PreToolUse hook** that holds the HTTP response until the 3DS makes a decision. This is a protocol-level fix, not a workaround.

### The Fix: Long-Poll Hook Handler

```typescript
// server.ts
const pendingDecisions = new Map<number, (decision: "allow" | "deny") => void>()

// PreToolUse — hold connection until 3DS decides
if (path === "/hook/pre-tool" && req.method === "POST") {
  const body = await req.json() as PreToolHook
  const slot = resolveSlot(body.session_id)
  const toolName = body.tool_name || "Unknown"
  const toolDetail = extractToolDetail(body.tool_input ?? {})

  // Check auto-edit shortcut
  if (isAutoEditEnabled() && isEditTool(toolName)) {
    return Response.json({
      hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "allow" }
    })
  }

  // Broadcast waiting state to 3DS
  updateState(slot, { state: "waiting", promptToolType: toolName, promptToolDetail: toolDetail })

  // Hold HTTP connection (up to 30 seconds)
  const decision = await new Promise<"allow" | "deny">((resolve) => {
    pendingDecisions.set(slot, resolve)
    setTimeout(() => resolve("deny"), 30_000)  // Timeout = deny
  })
  pendingDecisions.delete(slot)

  updateState(slot, { state: "working", promptToolType: undefined })
  return Response.json({
    hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: decision }
  })
}

// 3DS sends decision via WebSocket → resolve the pending promise
if (msg.type === "action") {
  const resolver = pendingDecisions.get(targetSlot)
  if (resolver) {
    resolver(msg.action === "yes" || msg.action === "always" ? "allow" : "deny")
  }
}
```

**This single change:**
1. Eliminates `scraper.ts` entirely
2. Eliminates `tmux send-keys` for approval
3. Makes approval decisions atomic and reliable
4. Provides proper "always" behavior (set auto-edit)
5. Handles timeout gracefully (deny if 3DS doesn't respond)

---

## Phased Improvement Plan

### Phase 1: Drop the Scraper (High Priority)

**What:** Replace tmux screen scraping with blocking HTTP hook + long-poll decision.

**Changes:**
1. Change `hooks.ts` to install `type: "http"` PreToolUse hook (or keep `curl` but fix the handler to block)
2. Add `pendingDecisions` Map to `server.ts`
3. Change WebSocket `action` handler to resolve pending decisions
4. Delete `scraper.ts` and all references
5. Remove `tmux send-keys` from `claude.ts` adapter (keep for `sendInput` — sending prompts)
6. Register `Notification` hook as backup for any prompts that come through non-PreToolUse paths
7. Register `SessionStart` and `SessionEnd` hooks

**Impact:** This fixes the core reliability problem. The 3DS approval flow becomes deterministic.

### Phase 2: Claude Code Channels (Medium Priority)

**What:** Implement a Channel MCP server for permission relay.

**Why:** Channels are the official Anthropic path. They give richer tool data (the full MCP call context), work with the future API surface, and are documented.

**Changes:**
1. Create `companion-server/src/channel.ts` — MCP server declaring `permission_relay` capability
2. Channel server and companion server share the `pendingDecisions` Map
3. Write a `raids` wrapper script: `claude --channels ./companion-server/channel.ts "$@"`
4. Update README with channel setup

**Result:** Users who use the `raids` wrapper get full channel integration; users who run plain `claude` fall back to HTTP hooks.

### Phase 3: Agent SDK for Spawned Sessions (Lower Priority)

**What:** Use `@anthropic-ai/claude-agent-sdk` for sessions spawned from the 3DS (slots 1–3).

**Why:** SDK sessions get `canUseTool` callback integration (no hooks needed), proper session lifecycle management, and structured message streaming.

**Changes:**
1. `npm install @anthropic-ai/claude-agent-sdk`
2. Change `spawnSession()` in `session.ts` to use SDK `query()` instead of `tmux new-session claude`
3. SDK-based sessions don't need tmux adapters; decisions flow through `canUseTool` callback
4. Stream assistant messages back to 3DS (currently not shown)

**Result:** Spawned sessions are more reliable and show richer data.

### Phase 4: 3DS UI Enhancements

**What:** Improve the 3DS app UI for the improved data model.

**Changes:**
1. **Prompt input:** Add swkbd prompt entry (bottom screen text field)
2. **Audio alerts:** Play chime when waiting for approval (the primary call-to-action)
3. **Tool detail scrolling:** Circle pad to scroll long commands/file paths
4. **History screen:** Show last 5 tools used (from JSONL event log)
5. **Touchscreen controls:** Bottom screen tap for Yes/No/Always buttons
6. **mDNS discovery:** UDP beacon so server IP doesn't need to be hardcoded

### Phase 5: Voice Input (Future)

**What:** Use the 3DS microphone to send voice prompts to companion server.

**Architecture:**
```
3DS mic → PCM16 audio → companion server → local whisper model → text → Claude
```

**Requirements:**
- Local whisper model running on the host machine (whisper.cpp via HTTP)
- 3DS streams audio over WebSocket (binary frames)
- Companion server buffers audio, sends to whisper, receives text, sends to Claude

This is technically feasible given all the pieces exist.

---

## Architecture Target State

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                              Host Machine                                        │
│                                                                                   │
│  ┌──────────────────────────────────────────────────────────────────────────┐   │
│  │                        Companion Server (port 3333)                       │   │
│  │                                                                            │   │
│  │  HTTP hooks (blocking PreToolUse)   WebSocket hub   Channel MCP server   │   │
│  │       ↕                                  ↕                  ↕            │   │
│  │  pendingDecisions Map            3DS clients        Claude Code           │   │
│  └────────────────────────────────────────────────────────────────────────── ┘  │
│            ↕                                              ↕                       │
│  ┌─────────────────────┐                    ┌────────────────────────────────┐  │
│  │  Claude Code (slot 0)│                   │  Agent SDK sessions (slots 1-3) │  │
│  │  [user's terminal]   │                   │  [spawned from 3DS]             │  │
│  │  hooks → long-poll   │                   │  canUseTool callback            │  │
│  └─────────────────────┘                    └────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────┘
                                    ↕ WiFi WebSocket
                         ┌─────────────────────────────┐
                         │         Nintendo 3DS          │
                         │                               │
                         │  Top: Agent status + anim     │
                         │  Bottom: Approval + controls  │
                         │  Buttons: A=Yes B=No Y=Auto   │
                         │  Touchscreen: bottom buttons  │
                         └─────────────────────────────┘
```

---

## What NOT to Change

1. **The WebSocket transport** — it works, it's right for this use case
2. **tmux for slot 0** — developers want to keep their existing terminal session
3. **The JSON protocol format** — it's clean and extensible
4. **The Pokémon-style UI concept** — this is the soul of the project, keep developing it
5. **The companion server language (Bun/TypeScript)** — excellent choice, fast, modern
6. **The C/libctru 3DS app** — correct choice, only native language gets full hardware access

---

## Principles for Future Development

1. **Structured over scraped** — never parse terminal output if there's a structured API
2. **Blocking over polling** — hold HTTP connections open rather than polling for state
3. **Graceful degradation** — if 3DS disconnects, Claude Code should continue (not hang)
4. **Separation of concerns** — the companion server is the intelligence layer; the 3DS is the UI layer
5. **Official APIs first** — prefer hooks > scraping, prefer Channels > hooks where available
6. **LAN-only is a feature** — no cloud relay, no accounts, sub-millisecond local latency
7. **Hardware identity** — the 3DS form factor (buttons, clamshell, dedicated device) is the product's differentiator from phone-based tools
