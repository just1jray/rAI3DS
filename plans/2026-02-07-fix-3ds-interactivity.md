# Fix 3DS Interactivity & Context Display

## Problems
1. `$CLAUDE_TOOL_NAME` sent literally (single quotes prevent expansion)
2. Approve/deny buttons don't work (case mismatch + non-blocking hooks)
3. Tool options don't show on bottom screen
4. Top bar progress is useless - should show context usage

## Architecture Change: Blocking PreToolUse Hook

The core fix: PreToolUse hook must **block** until the 3DS user responds.

**Current flow (broken):**
```
Hook fires → curl POST (fire-and-forget, returns immediately) → exit 0 (auto-approve)
```

**New flow:**
```
Hook fires → curl POST (long-poll, blocks) → server holds request open →
  broadcasts to 3DS → 3DS shows tool + buttons → user taps approve/deny →
  server resolves pending request → curl gets response → exit 0 or 2
```

## Tasks

### Task 1: Fix hook commands (hooks.ts)
- Change single quotes to double quotes so `$CLAUDE_TOOL_NAME` expands
- PreToolUse: blocking curl with `--max-time 300` (5 min timeout)
- Parse response to determine exit code (0=approve, 2=deny)
- PostToolUse: fire-and-forget (keep simple)

Hook command for PreToolUse becomes:
```bash
RESULT=$(curl -s --max-time 300 -X POST http://localhost:3333/hook/pre-tool -H "Content-Type: application/json" -d "{\"tool\":\"$CLAUDE_TOOL_NAME\"}"); case "$RESULT" in *"deny"*) exit 2;; *) exit 0;; esac
```

PostToolUse:
```bash
curl -s -X POST http://localhost:3333/hook/post-tool -H "Content-Type: application/json" -d "{\"tool\":\"$CLAUDE_TOOL_NAME\"}" > /dev/null 2>&1 &
```

### Task 2: Server long-polling for PreToolUse (server.ts)
- `/hook/pre-tool` POST: set state to "waiting", broadcast to 3DS, hold response open
- Store a `pendingResolve` callback that resolves when 3DS user acts
- When 3DS sends approve/deny via WebSocket, resolve the pending request
- Return `{"action":"approve"}` or `{"action":"deny"}`
- Timeout after 5 minutes → auto-approve
- Remove `/hook/waiting` endpoint (no longer needed)

### Task 3: Wire WebSocket actions to resolve pending (websocket.ts + server.ts)
- When 3DS sends `{"type":"action","action":"approve"}`, call server's resolve function
- Fix case sensitivity: use `toLowerCase()` or `localeCompare` for agent name matching
- Export a `resolveToolAction` function from server.ts

### Task 4: Context tracking (new: context.ts)
- Find the current Claude transcript file: scan `~/.claude/projects/-Users-jesse-Developer-rAI3DS/` for most recent `.jsonl`
- Read last usage entry (jq-style parsing): extract `input_tokens + cache_read + cache_creation`
- Calculate percentage against 200k context window
- Expose as a function, call every few seconds
- Add `contextPercent` field to the AgentStatusMessage broadcast

### Task 5: Update protocol.h and network.c (3DS)
- Add `context_percent` field to Agent struct (int, 0-100)
- Parse `contextPercent` from incoming JSON in `parse_agent_status`

### Task 6: Update 3DS UI (ui.c)
- **Top screen**: Replace progress bar with context usage bar
  - Show "Context: XX%" with colored bar (green→yellow→red as it fills)
  - Show max context (e.g., "200k")
- **Bottom screen**: When waiting state:
  - Show tool name prominently at top
  - Large APPROVE button (green)
  - Large DENY button (red)
  - Tool details area
- When not waiting: show "Idle - waiting for tool call" or current status

### Task 7: Reinstall hooks + test
- Run `bun run /path/to/index.ts uninstall` then `install`
- Verify hooks in settings.json use double quotes
- Build 3DS app with docker
- Test end-to-end
