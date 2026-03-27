# Remote Control Architecture Research Summary

*Date: 2026-03-27*
*Full research: `research/` directory*

## Verdict

The rAI3DS architecture is fundamentally sound. The WebSocket transport, hooks system, tmux session management, and Bun/TypeScript server are all correct choices. Specific sub-components need improvement.

**Do not rewrite. Evolve.**

## The One Critical Bug

`scraper.ts` is fragile screen-scraping that must be replaced. The fix is a **blocking PreToolUse hook**:

- Change the PreToolUse HTTP handler to hold the connection open
- Broadcast `state: "waiting"` to the 3DS
- Resolve the HTTP response when the 3DS sends a WebSocket decision message
- Respond with `{ hookSpecificOutput: { permissionDecision: "allow"|"deny" } }`

This is one file change (~30 lines) that eliminates all scraping.

## Key New Finding: Claude Code Channels

Claude Code v2.1.80 (March 20, 2026) added `--channels` with **permission_relay** capability. This is Anthropic's official mechanism for what rAI3DS is trying to do. A channel MCP server that forwards permission prompts to the 3DS is the architecturally correct long-term path.

## SDK Usage

The Claude Agent SDK (`@anthropic-ai/claude-agent-sdk`) `canUseTool` callback is right for **spawned** sessions (3DS-initiated, slots 1–3). The primary developer session (slot 0) stays on hooks.

## SSH

Rejected. Wrong paradigm, no 3DS library, terminal rendering complexity. Not worth pursuing.

## Phased Plan

| Phase | Work | Impact |
|-------|------|--------|
| 1 | Blocking hook handler + kill scraper | Fixes reliability |
| 2 | Channel MCP server | Official Channels integration |
| 3 | Agent SDK for spawned sessions | Better spawned session UX |
| 4 | 3DS UI: swkbd, audio alerts, scrolling | Better UX |
| 5 | Voice input via 3DS mic + whisper | Novel feature |
