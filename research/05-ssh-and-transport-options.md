# Transport Layer Options Analysis

*Researched: 2026-03-27*

## Current Transport: WebSocket over WiFi

The 3DS app maintains a persistent WebSocket connection to the companion server on port 3333. The server uses Bun's native WebSocket support.

```
3DS WiFi → TCP → WebSocket upgrade → JSON messages (bidirectional)
```

---

## Option 1: Keep WebSocket (Recommended)

### Rationale

WebSocket is the right choice for this use case:
- Bidirectional, low-latency
- Well understood protocol with good server-side tooling
- Bun has native, performant WebSocket support
- The 3DS custom implementation works (just needs polish)

### Improvements Needed

**Current issues in `network.c`:**
- No WebSocket ping/pong keepalive (connections go stale silently)
- No fragmented message reassembly
- JSON parsed inline without buffer protection

**Fixes:**
```c
// Add ping/pong
#define WS_PING_INTERVAL 30  // seconds
#define WS_PONG_TIMEOUT  5   // seconds after ping
```

**Consider MessagePack instead of JSON:**
- ~30% smaller messages
- Faster parse on ARM11
- Requires a C MessagePack library (tiny ones exist: mpack.h is ~2KB)
- Worth it if battery life or latency matters; not critical for current usage

---

## Option 2: HTTP Long-Polling (SSE-like)

Replace WebSocket with Server-Sent Events pattern:

```c
// 3DS: GET /events (holds connection, reads newline-delimited JSON)
// 3DS: POST /action (sends approval decisions)
```

### Pros
- Simpler to implement correctly than WebSocket
- Standard HTTP — easier to debug with curl
- One-way server push + separate POST channel is conceptually clean

### Cons
- Higher latency (new TCP connection per POST)
- More HTTP overhead
- Not as clean as WebSocket for bidirectional communication

### Verdict: Not recommended. WebSocket is already working and is superior.

---

## Option 3: Raw TCP with Length-Prefix Protocol

The simplest possible protocol:

```
┌────────────────┬──────────────────────────────┐
│ uint32_t len   │ JSON payload (len bytes)      │
└────────────────┴──────────────────────────────┘
```

### Pros
- Trivial to implement on 3DS (no protocol parsing)
- No HTTP or WebSocket framing overhead
- Easy to test with netcat

### Cons
- No multiplexing without adding a message type field
- No browser-compatible — can't use wscat or browser devtools to test
- Would require changing the server

### Verdict: Useful for debugging, not worth replacing WebSocket.

---

## Option 4: SSH Client (Rejected)

See `04-3ds-homebrew-patterns.md` for full analysis. Summary:

- No mature SSH library for 3DS (libssh2 not in devkitPro)
- Terminal rendering is a separate large problem
- Wrong paradigm for structured game-controller UI

**Firmly rejected.** The SSH approach would make the 3DS a generic terminal emulator, not a purposeful companion app.

---

## Option 5: Claude Code Channels (MCP stdio)

*The most exciting new option — see `03-agent-sdk-and-hooks.md` for full analysis.*

Channels use MCP over stdio between Claude Code and a channel server process. The channel server can then communicate with the 3DS however it wants (WebSocket remains the 3DS transport).

```
Claude Code → MCP stdio → channel-server.ts → WebSocket → 3DS
```

This is orthogonal to the 3DS transport — channels affect the Claude Code ↔ server protocol, not the server ↔ 3DS protocol.

---

## Option 6: Local UDP Broadcast for Discovery

Not a replacement for the main transport, but solves the IP address configuration problem:

**Server (UDP beacon):**
```typescript
const udp = dgram.createSocket('udp4')
udp.bind(() => {
  udp.setBroadcast(true)
  setInterval(() => {
    const msg = Buffer.from(JSON.stringify({ type: "raids_beacon", port: 3333 }))
    udp.send(msg, 0, msg.length, 3334, '255.255.255.255')
  }, 2000)
})
```

**3DS (listen for beacon):**
```c
// UDP socket on port 3334
// Listen for beacon, extract sender IP
// Store as server address
// Proceed with WebSocket connect
```

This eliminates the hardcoded `config.h` IP address.

---

## Transport Decision Matrix

| Protocol | 3DS Complexity | Latency | Bidirectional | Discovery | Verdict |
|----------|---------------|---------|--------------|-----------|---------|
| WebSocket (current) | Medium | Low | Yes | Manual IP | Keep + improve |
| HTTP SSE + POST | Low | Medium | Partial | Manual IP | Not worth switch |
| Raw TCP | Low | Low | Yes | Manual IP | Useful for debug only |
| SSH | Very High | Low | Yes | Manual IP | Rejected |
| Channels (server-side) | N/A | Low | Via MCP | N/A | Add in Phase 2 |
| UDP Discovery | Low | N/A | No | Automatic | Add as enhancement |

---

## ralv.ai Architecture (Inspirational)

ralv.ai is a commercial product with similar goals to rAI3DS. Based on public descriptions and the pattern of similar products:

- Likely uses a local agent that runs on your machine (similar to companion server)
- Mobile app connects to the local agent via the cloud or LAN
- Permission prompts are forwarded to the mobile app
- Voice input is a key feature

rAI3DS has an advantage: it's fully local (LAN-only), which is simpler and more private. ralv.ai's cloud relay introduces latency and privacy concerns that rAI3DS avoids by design.

The 3DS form factor (physical buttons, clamshell design, dedicated hardware) also differentiates rAI3DS from phone-based controllers — it's a dedicated device with a purpose-built UI, not a repurposed phone.
