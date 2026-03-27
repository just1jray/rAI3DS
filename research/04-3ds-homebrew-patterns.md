# Nintendo 3DS Homebrew Architecture Patterns

*Researched: 2026-03-27*

## 3DS Hardware Constraints

| Resource | Specification |
|----------|---------------|
| CPU | Dual ARM11 @ 268MHz (old 3DS) / 804MHz (new 3DS) |
| RAM | 128MB total, ~48–64MB available to homebrew |
| Screens | Top: 400×240 (or 800×240 wide mode, stereoscopic), Bottom: 320×240 (touchscreen) |
| WiFi | IEEE 802.11b/g/n (no 5GHz) |
| Audio | DSP, 2ch stereo PCM |
| Storage | SD card, ~200MB ARM11 code limit |

## Networking Stack

### libctru Network Layer

The 3DS uses **libctru's socket layer** which wraps the `soc:U` system service:

```c
#include <3ds.h>
socInit(socBuffer, socBufferSize);  // Initialize with buffer
// Then use standard BSD socket API
socket(), connect(), send(), recv(), select()
socExit();
```

- **Supports:** TCP, UDP, IPv4
- **Limitations:** No IPv6, limited concurrent sockets (~16 max), blocking `connect()` can hang

### TLS/SSL via mbedtls

mbedtls is available in devkitPro (`-lmbedtls -lmbedx509 -lmbedcrypto`). It works but:
- Memory intensive (~64KB+ for TLS context)
- Adds link-time complexity
- CA certificate management is manual

**Many homebrew apps skip TLS** for LAN-only communication. This is acceptable for rAI3DS since the companion server is on the local network.

### DNS Resolution

DNS is available via `gethostbyname()` in libctru, but it can block for seconds on lookup failure. The `first_connection_done` pattern in rAI3DS (defer first connect until after first frame) correctly handles this.

## WebSocket on 3DS

The current rAI3DS WebSocket implementation is custom (in `network.c`). Alternatives:

### Option A: Custom (current)
- Hand-rolled WebSocket client in `network.c`
- Pro: Minimal, no dependencies
- Con: Feature incomplete (no ping/pong, no fragmentation), fragile with server changes

### Option B: libwebsockets port
- Several 3DS homebrew devs have attempted porting libwebsockets
- None are in mainstream devkitPro packages
- Heavy (100KB+)

### Option C: Simple HTTP polling
- Replace WebSocket with HTTP long-polling or SSE
- The 3DS sends a GET request, server holds it open and sends newline-delimited JSON
- Much simpler to implement correctly than WebSocket
- Slightly higher latency (one RTT per event instead of persistent connection)
- **Recommended for reliability if WebSocket proves unstable**

### Option D: Raw TCP with length-prefixed JSON
- Simplest possible binary protocol
- Server: write `uint32_t length` + JSON bytes
- 3DS: read length, read JSON, parse
- No WebSocket overhead, no HTTP overhead
- Easiest to debug with netcat

## SSH on 3DS — Feasibility Assessment

**Short answer: Not recommended for rAI3DS.**

### Why Not

1. **No mature SSH client library for 3DS** — libssh2 requires mbedtls or OpenSSL, neither ships as a ready-to-use devkitPro package. Porting effort would be weeks.

2. **Wrong paradigm for structured control** — SSH gives you a terminal stream. Rendering a full terminal on a 400×240 display with C and citro2d requires a VT100 parser, font rendering, scroll buffer management. That's a separate project.

3. **Memory** — A full SSH + terminal stack would likely consume 30–50% of the available application memory.

4. **Use case mismatch** — rAI3DS wants *structured control* (show tool name, file path, a few buttons). SSH gives raw terminal data that must be parsed anyway.

### What SSH Would Add

- Direct tmux attach (bypass companion server)
- Could type arbitrary commands
- Works without any server-side changes

### What SSH Loses

- The companion server is where intelligence lives (session routing, multi-agent, auto-edit logic)
- Structured JSON protocol is much better for a game-controller UI
- The 3DS becomes a dumb terminal instead of a purpose-built controller

**Conclusion:** The current structured WebSocket protocol is the right paradigm. SSH is a fallback for power users, not the primary design.

## 3DS UI Patterns from Homebrew

### Software Keyboard (swkbd)

```c
SwkbdState swkbd;
char inputBuf[256];
swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, 255);
swkbdSetHintText(&swkbd, "Enter prompt...");
SwkbdButton btn = swkbdInputText(&swkbd, inputBuf, sizeof(inputBuf));
if (btn == SWKBD_BUTTON_CONFIRM) {
    // User typed something
}
```

Available types: `SWKBD_TYPE_NORMAL`, `SWKBD_TYPE_QWERTY`, `SWKBD_TYPE_NUMPAD`

**Use case for rAI3DS:** Prompt input from 3DS. Currently absent from the app.

### Error Applet

```c
errorConf errConf;
errorInit(&errConf, ERROR_TEXT, CFG_LANGUAGE_EN);
errorText(&errConf, "Connection failed");
errorDisp(&errConf);
```

### File Selection

```c
// Not a system applet — implement custom or use romfs
```

### Microphone Input

```c
#include <3ds/services/mic.h>
MICU_StartSampling(MICU_ENCODING_PCM16_SIGNED, MICU_SAMPLE_RATE_8180,
                   offset, size, loop);
// Read samples from shared memory
u8* micBuffer = (u8*)MICU_GetSharedMemOffsetTable();
```

Available sample rates: 8180, 10910, 16360, 32730 Hz
Format: PCM16 signed

**Use case for rAI3DS:** Voice prompt input. Feed audio to companion server's whisper endpoint.

## Navigation Patterns

### Pokémon-Style Menus

The Pokémon inspiration maps perfectly to 3DS navigation:

| Game Pattern | rAI3DS Equivalent |
|-------------|------------------|
| Party screen (6 Pokémon) | 4 agent slots |
| Battle menu (Fight/Item/Run/Pokémon) | Actions (Yes/No/Always/Escape) |
| Name entry | Prompt input via swkbd |
| Move details screen | Tool detail: type + file/command |
| Status screen | Session info: state, context%, message |
| Overworld walking | Idle animation |
| Battle animation | Working animation |
| Waiting for move | Waiting/approval animation |

### Button Mapping Recommendation

```
Top screen: Status display (agent state, context %, recent tools)
Bottom screen: Interactive controls (approval buttons, agent select)

A button:     Approve / Yes
B button:     Deny / No
X button:     Always approve
Y button:     Toggle auto-edit
D-pad up/down: Navigate agent slots
Start:        Back / Exit
Select:       Settings menu
L/R:          Cycle agents
Circle pad:   Scroll tool detail text
Touchscreen:  Tap to select agent, tap buttons
```

### State-Based Screen Layout

```
IDLE state:
  Top: Creature idle animation, agent name, context bar
  Bottom: "Waiting for activity..." + slot buttons

WORKING state:
  Top: Creature working animation, current tool name + detail
  Bottom: Progress indicator, auto-edit toggle

WAITING state (most important):
  Top: Creature "waiting" animation (attention-grabbing)
  Bottom: Tool type (large), file/command (scrollable), [YES] [NO] [ALWAYS]
  → This is the core interaction screen
```

## Audio Patterns

### Using DSP/NDSP for Sound

The current `audio.c` exists but is minimal. Good sound design:
- **Idle:** Soft ambient loop (low priority)
- **Working:** Subtle activity sound
- **Waiting:** Attention-getting chime (like Pokémon "it's your turn!")
- **Approved:** Positive confirmation
- **Denied:** Negative feedback
- **Error:** Error chime

3DS audio via NDSP:
```c
ndspInit();
ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
ndspChnSetRate(0, sampleRate);
ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
// Play: ndspChnWaveBufAdd(0, &waveBuf)
```

## Build System

The current Docker-based devkitPro build is the right approach:
- No local devkitPro installation needed
- Reproducible builds
- `docker compose run --rm 3ds-build`

One improvement: add CI to build the `.3dsx` on each push (GitHub Actions with the devkitPro Docker image).

## Networking Architecture Recommendation

Given 3DS constraints, a minimal but reliable protocol:

### Keep WebSocket (current choice is good)

The current WebSocket implementation works. Focus on:
1. **Proper ping/pong** — detect stale connections faster
2. **Binary message support** — optionally use MessagePack instead of JSON for lower bandwidth
3. **Reconnect backoff** — current is fixed 2-second, use exponential up to 30s

### mDNS Discovery (future)

Hardcoded IP in `config.h` is painful. Consider:
- Server broadcasts UDP beacon on LAN (simple, no mDNS library needed)
- 3DS listens on a known port at startup, stores received IP
- Or: QR code on 3DS screen with server URL (scan to configure)

### Message Size Budget

A complete `agent_status` JSON message:
```json
{
  "type": "agent_status",
  "agent": "claude",
  "state": "waiting",
  "progress": -1,
  "message": "Bash: git status",
  "contextPercent": 45,
  "promptToolType": "Bash",
  "promptToolDetail": "git -C /home/user/project status",
  "promptDescription": "Show working tree status",
  "autoEdit": false,
  "slot": 0,
  "active": true
}
```
~300 bytes — well within 3DS network budget. Even with 4 slots broadcasting simultaneously, this is trivial.
