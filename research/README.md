# rAI3DS Architecture Research

*Date: 2026-03-27*

## Files

| File | Contents |
|------|----------|
| [01-current-architecture.md](./01-current-architecture.md) | Deep dive into the existing codebase — what each module does, data flow, known weaknesses |
| [02-reference-projects.md](./02-reference-projects.md) | Analysis of ClawdGotchi, Vibecraft, T3Code, Agent Deck — protocols, patterns, lessons |
| [03-agent-sdk-and-hooks.md](./03-agent-sdk-and-hooks.md) | Claude Agent SDK, hooks system, `canUseTool`, HTTP hooks, and **Claude Code Channels** |
| [04-3ds-homebrew-patterns.md](./04-3ds-homebrew-patterns.md) | 3DS hardware constraints, networking, UI patterns, audio, mic input, build system |
| [05-ssh-and-transport-options.md](./05-ssh-and-transport-options.md) | Full transport comparison (WebSocket, HTTP SSE, raw TCP, SSH, Channels) |
| [06-design-recommendations.md](./06-design-recommendations.md) | Verdict + phased improvement plan with concrete code changes |

## TL;DR Answers

### Should the design stay as-is?

**Mostly yes.** The fundamental choices (WebSocket transport, hooks system, Bun/TypeScript server, C/libctru 3DS app, tmux session management) are all correct. The architecture is sound.

**One critical flaw must be fixed:** `scraper.ts` (tmux screen scraping) is fragile and unnecessary. Replace it with a **blocking PreToolUse HTTP hook** that holds the response until the 3DS makes a decision.

### Should it convert to the Agent SDK?

**Partially.** The Claude Agent SDK (`@anthropic-ai/claude-agent-sdk`) has a `canUseTool` callback that is perfect for sessions *spawned from the 3DS* (slots 1–3). For the primary developer session (slot 0 — the `claude` terminal the developer already has open), the SDK doesn't apply; hooks are the right mechanism.

### Should it convert to an SSH client?

**No.** SSH is the wrong paradigm. It gives you a raw terminal stream that must be parsed (trading one scraping problem for another). The structured JSON protocol over WebSocket is far better for a game-controller UI. No mature SSH client library exists for 3DS anyway.

### What about Claude Code Channels?

**Yes — add this.** Claude Code v2.1.80 (released March 20, 2026 — just 7 days ago) added `--channels` support with **permission relay** capability. This is Anthropic's official mechanism for forwarding permission prompts to remote clients. Implementing a channel MCP server makes rAI3DS the reference implementation for this pattern.

## Top 3 Concrete Next Steps

1. **Kill the scraper** — rewrite `server.ts` PreToolUse handler to hold HTTP connection open while waiting for 3DS WebSocket decision. Delete `scraper.ts`.

2. **Add `Notification` + `SessionStart`/`SessionEnd` hooks** — register the missing hook events in `hooks.ts`.

3. **Implement a Channel MCP server** — create `companion-server/src/channel.ts` that declares `permission_relay` capability, enabling the official Channels integration for users who use the `raids` wrapper script.
