# rAI3DS MVP Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a working 3DS companion app that can approve/deny Claude Code tool executions over WiFi.

**Architecture:** Companion server (Bun/TypeScript) receives Claude Code hook events via HTTP, manages agent state, and relays to 3DS app over WebSocket. 3DS app (C/libctru) displays status and sends approve/deny actions back. Server controls Claude Code via tmux session.

**Tech Stack:** Bun, TypeScript, WebSocket (ws), Docker | devkitPro Docker image, libctru, citro2d, cJSON

---

## Task 0: Project Setup & Containerization

**Files:**
- Create: `docker-compose.yml`
- Create: `companion-server/Dockerfile`
- Create: `companion-server/package.json`
- Create: `companion-server/tsconfig.json`
- Create: `companion-server/src/index.ts`
- Create: `3ds-app/Dockerfile`
- Create: `3ds-app/Makefile`
- Create: `3ds-app/source/main.c`
- Create: `.gitignore`

### Step 1: Create .gitignore

```gitignore
# Node
node_modules/
*.log

# Bun
bun.lockb

# 3DS build artifacts
3ds-app/build/
3ds-app/*.3dsx
3ds-app/*.elf
3ds-app/*.smdh
3ds-app/*.cia

# IDE
.vscode/
.idea/

# OS
.DS_Store
Thumbs.db

# Docker
.docker/
```

### Step 2: Create docker-compose.yml

```yaml
version: "3.8"

services:
  companion-server:
    build: ./companion-server
    ports:
      - "3333:3333"  # HTTP (hooks)
      - "3334:3334"  # WebSocket (3DS)
    volumes:
      - ./companion-server/src:/app/src
      - ~/.claude:/root/.claude  # For hook installation
    environment:
      - NODE_ENV=development

  3ds-build:
    image: devkitpro/devkitarm:latest
    volumes:
      - ./3ds-app:/app
    working_dir: /app
    command: make
```

### Step 3: Create companion-server/Dockerfile

```dockerfile
FROM oven/bun:1

WORKDIR /app

COPY package.json bun.lockb* ./
RUN bun install

COPY . .

EXPOSE 3333 3334

CMD ["bun", "run", "dev"]
```

### Step 4: Create companion-server/package.json

```json
{
  "name": "raids-companion-server",
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "bun run --watch src/index.ts",
    "start": "bun run src/index.ts",
    "test": "bun test"
  },
  "dependencies": {
    "ws": "^8.16.0"
  },
  "devDependencies": {
    "@types/bun": "latest",
    "@types/ws": "^8.5.10"
  }
}
```

### Step 5: Create companion-server/tsconfig.json

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ESNext",
    "moduleResolution": "bundler",
    "strict": true,
    "skipLibCheck": true,
    "noEmit": true,
    "types": ["bun-types"]
  },
  "include": ["src/**/*"]
}
```

### Step 6: Create companion-server/src/index.ts (placeholder)

```typescript
console.log("rAI3DS Companion Server starting...");
console.log("HTTP server will listen on :3333");
console.log("WebSocket server will listen on :3334");
```

### Step 7: Create 3ds-app/Makefile

```makefile
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# DATA is a list of directories containing data files
# INCLUDES is a list of directories containing header files
# GRAPHICS is a list of directories containing graphics files
#---------------------------------------------------------------------------------
TARGET      := raids
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include
GRAPHICS    := gfx

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS      := -g -Wall -O2 -mword-relocations \
               -ffunction-sections \
               $(ARCH)

CFLAGS      += $(INCLUDE) -D__3DS__

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS     := -g $(ARCH)
LDFLAGS     = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -lcitro2d -lcitro3d -lctru -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS     := $(CTRULIB)


#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(GRAPHICS),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
PICAFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.v.pica)))
SHLISTFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.shlist)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))
T3XFILES    := $(foreach dir,$(GRAPHICS),$(notdir $(wildcard $(dir)/*.t3s)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif
#---------------------------------------------------------------------------------

export OFILES_SOURCES := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES_BIN     := $(addsuffix .o,$(BINFILES)) $(T3XFILES:.t3s=.t3x.o)
export OFILES         := $(OFILES_BIN) $(OFILES_SOURCES)
export HFILES         := $(addsuffix .h,$(subst .,_,$(BINFILES))) $(T3XFILES:.t3s=.h)

export INCLUDE    := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                     $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                     -I$(CURDIR)/$(BUILD)

export LIBPATHS   := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf


#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).3dsx: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o %_bin.h: %.bin
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
%.t3x.o %_t3x.h: %.t3x
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@$(bin2o)

#---------------------------------------------------------------------------------
# rules for assembling GPU shaders
#---------------------------------------------------------------------------------
define shader-as
	$(eval CURBIN := $*.shbin)
	$(eval DESSION := $(subst .,_,$(shell echo $* | tr '[:lower:]' '[:upper:]')))
	$(DEVKITPRO)/tools/bin/picasso -o $(CURBIN) $1
	bin2s $(CURBIN) | $(AS) -o $*.shbin.o
	echo "extern const u8" $(DESSION)_shbin"[];" > `(echo $(CURBIN) | sed -e 's/\.shbin/_shbin.h/g')`
	echo "extern const u32" $(DESSION)_shbin_size";" >> `(echo $(CURBIN) | sed -e 's/\.shbin/_shbin.h/g')`
endef

%.shbin.o %_shbin.h: %.v.pica %.g.pica
	@echo $(notdir $^)
	@$(call shader-as,$^)

%.shbin.o %_shbin.h: %.v.pica
	@echo $(notdir $<)
	@$(call shader-as,$<)

%.shbin.o %_shbin.h: %.shlist
	@echo $(notdir $<)
	@$(call shader-as,$(foreach file,$(shell cat $<),$(dir $<)$(file)))

#---------------------------------------------------------------------------------
%.t3x: %.t3s
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	@tex3ds -i $< -o $*.t3x

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
```

### Step 8: Create 3ds-app/source/main.c (hello world)

```c
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>

#define TOP_SCREEN_WIDTH 400
#define TOP_SCREEN_HEIGHT 240
#define BOTTOM_SCREEN_WIDTH 320
#define BOTTOM_SCREEN_HEIGHT 240

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Colors
    u32 clrClear = C2D_Color32(0x1a, 0x1a, 0x2e, 0xFF);  // Dark blue background
    u32 clrWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);

    // Text buffer
    C2D_TextBuf textBuf = C2D_TextBufNew(256);
    C2D_Text txtTitle, txtStatus;

    // Prepare text
    C2D_TextParse(&txtTitle, textBuf, "rAI3DS v0.1.0");
    C2D_TextParse(&txtStatus, textBuf, "Press START to exit");
    C2D_TextOptimize(&txtTitle);
    C2D_TextOptimize(&txtStatus);

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Render top screen
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topScreen, clrClear);
        C2D_SceneBegin(topScreen);

        C2D_DrawText(&txtTitle, C2D_WithColor, 150.0f, 100.0f, 0.0f, 1.0f, 1.0f, clrWhite);

        // Render bottom screen
        C2D_TargetClear(bottomScreen, clrClear);
        C2D_SceneBegin(bottomScreen);

        C2D_DrawText(&txtStatus, C2D_WithColor, 80.0f, 110.0f, 0.0f, 0.8f, 0.8f, clrWhite);

        C3D_FrameEnd(0);
    }

    // Cleanup
    C2D_TextBufDelete(textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
```

### Step 9: Create 3ds-app/Dockerfile (convenience wrapper)

```dockerfile
FROM devkitpro/devkitarm:latest

WORKDIR /app

# Default command builds the project
CMD ["make"]
```

### Step 10: Verify server container builds

Run: `docker compose build companion-server`
Expected: Build succeeds, image created

### Step 11: Verify 3DS container builds app

Run: `docker compose run --rm 3ds-build`
Expected: Build succeeds, `raids.3dsx` created in 3ds-app/

### Step 12: Test server starts

Run: `docker compose up companion-server`
Expected: "rAI3DS Companion Server starting..." message

### Step 13: Commit

```bash
git init
git add .
git commit -m "chore: initial project setup with Docker containers

- Companion server: Bun/TypeScript with hot reload
- 3DS app: devkitPro Docker image for builds
- Hello world 3DS app with citro2d"
```

---

## Task 1: Companion Server HTTP Endpoints

**Files:**
- Create: `companion-server/src/server.ts`
- Modify: `companion-server/src/index.ts`
- Create: `companion-server/src/types.ts`

### Step 1: Create types.ts

```typescript
// Agent types
export type AgentName = "claude" | "codex" | "gemini" | "cursor";
export type AgentState = "working" | "waiting" | "idle" | "error" | "done";

// State stored per agent
export interface AgentStatus {
  name: AgentName;
  state: AgentState;
  progress: number; // 0-100, -1 for indeterminate
  message: string;
  pendingCommand?: string;
  lastUpdate: number;
}

// Hook payloads from Claude Code
export interface PreToolHook {
  tool: string;
  input?: Record<string, unknown>;
}

export interface PostToolHook {
  tool: string;
  output?: string;
  error?: string;
}

// Messages to 3DS
export interface AgentStatusMessage {
  type: "agent_status";
  agent: AgentName;
  state: AgentState;
  progress: number;
  message: string;
  pendingCommand?: string;
}

// Messages from 3DS
export interface UserAction {
  type: "action";
  agent: AgentName;
  action: "approve" | "deny" | "cancel";
}

export interface UserCommand {
  type: "command";
  agent: AgentName;
  command: string;
}

export type DSMessage = UserAction | UserCommand;
```

### Step 2: Create server.ts with HTTP endpoints

```typescript
import type { PreToolHook, PostToolHook, AgentStatus } from "./types";

const HTTP_PORT = 3333;

// In-memory state (will be moved to state.ts later)
const agentState: AgentStatus = {
  name: "claude",
  state: "idle",
  progress: -1,
  message: "Waiting for activity...",
  lastUpdate: Date.now(),
};

// Broadcast function (placeholder - will connect to WebSocket)
let broadcast: (status: AgentStatus) => void = () => {};

export function setBroadcast(fn: (status: AgentStatus) => void) {
  broadcast = fn;
}

export function getAgentState(): AgentStatus {
  return agentState;
}

function updateState(updates: Partial<AgentStatus>) {
  Object.assign(agentState, updates, { lastUpdate: Date.now() });
  broadcast(agentState);
}

// HTTP server using Bun's native server
export function startHttpServer() {
  const server = Bun.serve({
    port: HTTP_PORT,
    async fetch(req) {
      const url = new URL(req.url);
      const path = url.pathname;

      // Health check
      if (path === "/health" && req.method === "GET") {
        return Response.json({ status: "ok", agent: agentState });
      }

      // Pre-tool hook: tool execution starting
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          console.log(`[hook] pre-tool: ${body.tool}`);

          updateState({
            state: "working",
            progress: -1,
            message: `Running: ${body.tool}`,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Post-tool hook: tool execution finished
      if (path === "/hook/post-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PostToolHook;
          console.log(`[hook] post-tool: ${body.tool}`);

          updateState({
            state: body.error ? "error" : "idle",
            progress: -1,
            message: body.error || `Completed: ${body.tool}`,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Notification hook: waiting for approval
      if (path === "/hook/waiting" && req.method === "POST") {
        try {
          const body = (await req.json()) as { command?: string };
          console.log(`[hook] waiting for approval`);

          updateState({
            state: "waiting",
            progress: -1,
            message: "Waiting for approval",
            pendingCommand: body.command,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      return Response.json({ error: "Not found" }, { status: 404 });
    },
  });

  console.log(`HTTP server listening on http://localhost:${HTTP_PORT}`);
  return server;
}
```

### Step 3: Update index.ts to start HTTP server

```typescript
import { startHttpServer } from "./server";

console.log("rAI3DS Companion Server starting...");

startHttpServer();

console.log("Server ready. Waiting for hooks and 3DS connections...");
```

### Step 4: Test HTTP endpoints

Run: `docker compose up companion-server`

In another terminal:
```bash
# Test health
curl http://localhost:3333/health

# Test pre-tool hook
curl -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}'

# Test post-tool hook
curl -X POST http://localhost:3333/hook/post-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}'

# Test waiting hook
curl -X POST http://localhost:3333/hook/waiting \
  -H 'Content-Type: application/json' \
  -d '{"command":"rm -rf node_modules"}'
```

Expected: Each returns `{"ok":true}`, health shows updated state

### Step 5: Commit

```bash
git add companion-server/src/
git commit -m "feat(server): add HTTP endpoints for Claude Code hooks

- /hook/pre-tool: marks agent as working
- /hook/post-tool: marks agent as idle/error
- /hook/waiting: marks agent as waiting for approval
- /health: returns current agent state"
```

---

## Task 2: Companion Server WebSocket

**Files:**
- Create: `companion-server/src/websocket.ts`
- Modify: `companion-server/src/index.ts`
- Modify: `companion-server/src/server.ts`

### Step 1: Create websocket.ts

```typescript
import { WebSocketServer, WebSocket } from "ws";
import type { AgentStatus, DSMessage, AgentStatusMessage } from "./types";

const WS_PORT = 3334;

let wss: WebSocketServer;
const clients: Set<WebSocket> = new Set();

export function startWebSocketServer() {
  wss = new WebSocketServer({ port: WS_PORT });

  wss.on("connection", (ws) => {
    console.log("[ws] 3DS client connected");
    clients.add(ws);

    ws.on("message", (data) => {
      try {
        const msg = JSON.parse(data.toString()) as DSMessage;
        handleMessage(msg);
      } catch (e) {
        console.error("[ws] Invalid message:", e);
      }
    });

    ws.on("close", () => {
      console.log("[ws] 3DS client disconnected");
      clients.delete(ws);
    });

    ws.on("error", (err) => {
      console.error("[ws] Error:", err);
      clients.delete(ws);
    });
  });

  console.log(`WebSocket server listening on ws://localhost:${WS_PORT}`);
  return wss;
}

function handleMessage(msg: DSMessage) {
  console.log("[ws] Received:", msg);

  if (msg.type === "action") {
    // TODO: Send to Claude adapter
    console.log(`[ws] Action: ${msg.action} for ${msg.agent}`);
  } else if (msg.type === "command") {
    // TODO: Send to Claude adapter
    console.log(`[ws] Command: ${msg.command} for ${msg.agent}`);
  }
}

export function broadcast(status: AgentStatus) {
  const message: AgentStatusMessage = {
    type: "agent_status",
    agent: status.name,
    state: status.state,
    progress: status.progress,
    message: status.message,
    pendingCommand: status.pendingCommand,
  };

  const data = JSON.stringify(message);

  for (const client of clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(data);
    }
  }
}

export function getClientCount(): number {
  return clients.size;
}
```

### Step 2: Update index.ts to start both servers

```typescript
import { startHttpServer, setBroadcast, getAgentState } from "./server";
import { startWebSocketServer, broadcast } from "./websocket";

console.log("rAI3DS Companion Server starting...");

// Start HTTP server (for hooks)
startHttpServer();

// Start WebSocket server (for 3DS)
startWebSocketServer();

// Connect broadcast function
setBroadcast(broadcast);

console.log("Server ready. Waiting for hooks and 3DS connections...");
```

### Step 3: Test WebSocket with wscat

Run: `docker compose up companion-server`

In another terminal:
```bash
# Install wscat if needed
bun add -g wscat

# Connect as 3DS client
wscat -c ws://localhost:3334
```

In a third terminal:
```bash
# Trigger a hook - should see message in wscat
curl -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Bash"}'
```

Expected: wscat shows `{"type":"agent_status","agent":"claude","state":"working",...}`

### Step 4: Test sending action from 3DS

In wscat, type:
```json
{"type":"action","agent":"claude","action":"approve"}
```

Expected: Server logs `[ws] Action: approve for claude`

### Step 5: Commit

```bash
git add companion-server/src/
git commit -m "feat(server): add WebSocket server for 3DS communication

- Broadcasts agent status updates to connected 3DS clients
- Receives action/command messages from 3DS
- Auto-removes disconnected clients"
```

---

## Task 3: Claude Code Adapter with tmux

**Files:**
- Create: `companion-server/src/adapters/claude.ts`
- Modify: `companion-server/src/websocket.ts`
- Modify: `companion-server/src/index.ts`

### Step 1: Create claude.ts adapter

```typescript
import { $ } from "bun";

const TMUX_SESSION = "claude-raids";

export interface ClaudeAdapter {
  isRunning(): Promise<boolean>;
  start(command?: string): Promise<void>;
  stop(): Promise<void>;
  sendApproval(): Promise<void>;
  sendDenial(): Promise<void>;
  sendInput(text: string): Promise<void>;
}

export function createClaudeAdapter(): ClaudeAdapter {
  return {
    async isRunning() {
      try {
        await $`tmux has-session -t ${TMUX_SESSION}`.quiet();
        return true;
      } catch {
        return false;
      }
    },

    async start(command = "claude") {
      const running = await this.isRunning();
      if (running) {
        console.log(`[claude] Session ${TMUX_SESSION} already running`);
        return;
      }

      console.log(`[claude] Starting tmux session: ${TMUX_SESSION}`);
      await $`tmux new-session -d -s ${TMUX_SESSION} ${command}`;
    },

    async stop() {
      const running = await this.isRunning();
      if (!running) {
        console.log(`[claude] Session ${TMUX_SESSION} not running`);
        return;
      }

      console.log(`[claude] Stopping tmux session: ${TMUX_SESSION}`);
      await $`tmux kill-session -t ${TMUX_SESSION}`;
    },

    async sendApproval() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot approve: session not running");
        return;
      }

      console.log("[claude] Sending approval (y)");
      await $`tmux send-keys -t ${TMUX_SESSION} y Enter`;
    },

    async sendDenial() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot deny: session not running");
        return;
      }

      console.log("[claude] Sending denial (n)");
      await $`tmux send-keys -t ${TMUX_SESSION} n Enter`;
    },

    async sendInput(text: string) {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot send input: session not running");
        return;
      }

      console.log(`[claude] Sending input: ${text}`);
      await $`tmux send-keys -t ${TMUX_SESSION} ${text} Enter`;
    },
  };
}
```

### Step 2: Update websocket.ts to use adapter

```typescript
import { WebSocketServer, WebSocket } from "ws";
import type { AgentStatus, DSMessage, AgentStatusMessage } from "./types";
import type { ClaudeAdapter } from "./adapters/claude";

const WS_PORT = 3334;

let wss: WebSocketServer;
const clients: Set<WebSocket> = new Set();
let claudeAdapter: ClaudeAdapter | null = null;

export function setClaudeAdapter(adapter: ClaudeAdapter) {
  claudeAdapter = adapter;
}

export function startWebSocketServer() {
  wss = new WebSocketServer({ port: WS_PORT });

  wss.on("connection", (ws) => {
    console.log("[ws] 3DS client connected");
    clients.add(ws);

    ws.on("message", (data) => {
      try {
        const msg = JSON.parse(data.toString()) as DSMessage;
        handleMessage(msg);
      } catch (e) {
        console.error("[ws] Invalid message:", e);
      }
    });

    ws.on("close", () => {
      console.log("[ws] 3DS client disconnected");
      clients.delete(ws);
    });

    ws.on("error", (err) => {
      console.error("[ws] Error:", err);
      clients.delete(ws);
    });
  });

  console.log(`WebSocket server listening on ws://localhost:${WS_PORT}`);
  return wss;
}

async function handleMessage(msg: DSMessage) {
  console.log("[ws] Received:", msg);

  if (msg.type === "action" && msg.agent === "claude" && claudeAdapter) {
    if (msg.action === "approve") {
      await claudeAdapter.sendApproval();
    } else if (msg.action === "deny") {
      await claudeAdapter.sendDenial();
    }
  } else if (msg.type === "command" && msg.agent === "claude" && claudeAdapter) {
    await claudeAdapter.sendInput(msg.command);
  }
}

export function broadcast(status: AgentStatus) {
  const message: AgentStatusMessage = {
    type: "agent_status",
    agent: status.name,
    state: status.state,
    progress: status.progress,
    message: status.message,
    pendingCommand: status.pendingCommand,
  };

  const data = JSON.stringify(message);

  for (const client of clients) {
    if (client.readyState === WebSocket.OPEN) {
      client.send(data);
    }
  }
}

export function getClientCount(): number {
  return clients.size;
}
```

### Step 3: Update index.ts to create adapter

```typescript
import { startHttpServer, setBroadcast } from "./server";
import { startWebSocketServer, broadcast, setClaudeAdapter } from "./websocket";
import { createClaudeAdapter } from "./adapters/claude";

console.log("rAI3DS Companion Server starting...");

// Create Claude adapter
const claudeAdapter = createClaudeAdapter();
setClaudeAdapter(claudeAdapter);

// Start HTTP server (for hooks)
startHttpServer();

// Start WebSocket server (for 3DS)
startWebSocketServer();

// Connect broadcast function
setBroadcast(broadcast);

console.log("Server ready. Waiting for hooks and 3DS connections...");
console.log("Note: Claude Code session not started. Use CLI to start.");
```

### Step 4: Add tmux to Dockerfile

```dockerfile
FROM oven/bun:1

# Install tmux for Claude Code session management
RUN apt-get update && apt-get install -y tmux && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY package.json bun.lockb* ./
RUN bun install

COPY . .

EXPOSE 3333 3334

CMD ["bun", "run", "dev"]
```

### Step 5: Test adapter (manual)

```bash
# Rebuild container
docker compose build companion-server

# Start server
docker compose up companion-server

# In another terminal, exec into container
docker compose exec companion-server bash

# Manually test tmux
tmux new-session -d -s test-session "echo hello; sleep 60"
tmux has-session -t test-session  # Should succeed
tmux send-keys -t test-session "test" Enter
tmux kill-session -t test-session
```

### Step 6: Commit

```bash
git add companion-server/
git commit -m "feat(server): add Claude Code adapter with tmux control

- Start/stop Claude Code in managed tmux session
- Send approve/deny via tmux send-keys
- Wire up WebSocket actions to adapter"
```

---

## Task 4: CLI Commands

**Files:**
- Modify: `companion-server/src/index.ts`
- Create: `companion-server/src/hooks.ts`
- Modify: `companion-server/package.json`

### Step 1: Create hooks.ts for installation

```typescript
import { $ } from "bun";
import { existsSync } from "fs";
import { homedir } from "os";
import { join } from "path";

const CLAUDE_SETTINGS_PATH = join(homedir(), ".claude", "settings.json");

interface ClaudeSettings {
  hooks?: {
    PreToolUse?: Array<{ matcher: string; command: string }>;
    PostToolUse?: Array<{ matcher: string; command: string }>;
  };
  [key: string]: unknown;
}

const RAIDS_HOOKS = {
  PreToolUse: [
    {
      matcher: "*",
      command: `curl -s -X POST http://localhost:3333/hook/pre-tool -H 'Content-Type: application/json' -d '{"tool":"$CLAUDE_TOOL_NAME"}'`,
    },
  ],
  PostToolUse: [
    {
      matcher: "*",
      command: `curl -s -X POST http://localhost:3333/hook/post-tool -H 'Content-Type: application/json' -d '{"tool":"$CLAUDE_TOOL_NAME"}'`,
    },
  ],
};

export async function installHooks(): Promise<boolean> {
  console.log("[hooks] Installing rAI3DS hooks to Claude Code...");

  // Read existing settings or create new
  let settings: ClaudeSettings = {};

  if (existsSync(CLAUDE_SETTINGS_PATH)) {
    try {
      const content = await Bun.file(CLAUDE_SETTINGS_PATH).text();
      settings = JSON.parse(content);
      console.log("[hooks] Found existing Claude settings");
    } catch (e) {
      console.error("[hooks] Failed to parse existing settings:", e);
      return false;
    }
  } else {
    console.log("[hooks] Creating new Claude settings file");
    // Ensure .claude directory exists
    const claudeDir = join(homedir(), ".claude");
    await $`mkdir -p ${claudeDir}`;
  }

  // Merge hooks
  settings.hooks = settings.hooks || {};
  settings.hooks.PreToolUse = [
    ...(settings.hooks.PreToolUse || []).filter(
      (h) => !h.command.includes("localhost:3333")
    ),
    ...RAIDS_HOOKS.PreToolUse,
  ];
  settings.hooks.PostToolUse = [
    ...(settings.hooks.PostToolUse || []).filter(
      (h) => !h.command.includes("localhost:3333")
    ),
    ...RAIDS_HOOKS.PostToolUse,
  ];

  // Write settings
  try {
    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks installed successfully");
    console.log(`[hooks] Settings written to: ${CLAUDE_SETTINGS_PATH}`);
    return true;
  } catch (e) {
    console.error("[hooks] Failed to write settings:", e);
    return false;
  }
}

export async function uninstallHooks(): Promise<boolean> {
  console.log("[hooks] Removing rAI3DS hooks from Claude Code...");

  if (!existsSync(CLAUDE_SETTINGS_PATH)) {
    console.log("[hooks] No Claude settings file found");
    return true;
  }

  try {
    const content = await Bun.file(CLAUDE_SETTINGS_PATH).text();
    const settings: ClaudeSettings = JSON.parse(content);

    if (settings.hooks) {
      settings.hooks.PreToolUse = (settings.hooks.PreToolUse || []).filter(
        (h) => !h.command.includes("localhost:3333")
      );
      settings.hooks.PostToolUse = (settings.hooks.PostToolUse || []).filter(
        (h) => !h.command.includes("localhost:3333")
      );
    }

    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks removed successfully");
    return true;
  } catch (e) {
    console.error("[hooks] Failed to remove hooks:", e);
    return false;
  }
}
```

### Step 2: Update index.ts with CLI parsing

```typescript
import { startHttpServer, setBroadcast } from "./server";
import { startWebSocketServer, broadcast, setClaudeAdapter } from "./websocket";
import { createClaudeAdapter } from "./adapters/claude";
import { installHooks, uninstallHooks } from "./hooks";

const HELP = `
rAI3DS Companion Server

Usage:
  raids [command]

Commands:
  start       Start the companion server (default)
  install     Install Claude Code hooks
  uninstall   Remove Claude Code hooks
  help        Show this help message

Examples:
  raids              # Start server
  raids install      # Install hooks, then start server
  raids uninstall    # Remove hooks
`;

async function main() {
  const command = process.argv[2] || "start";

  switch (command) {
    case "help":
    case "--help":
    case "-h":
      console.log(HELP);
      process.exit(0);

    case "install":
      const installed = await installHooks();
      if (!installed) process.exit(1);
      // Fall through to start
      break;

    case "uninstall":
      const uninstalled = await uninstallHooks();
      process.exit(uninstalled ? 0 : 1);

    case "start":
      break;

    default:
      console.error(`Unknown command: ${command}`);
      console.log(HELP);
      process.exit(1);
  }

  // Start servers
  console.log("rAI3DS Companion Server starting...");

  const claudeAdapter = createClaudeAdapter();
  setClaudeAdapter(claudeAdapter);

  startHttpServer();
  startWebSocketServer();
  setBroadcast(broadcast);

  console.log("Server ready. Waiting for hooks and 3DS connections...");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
```

### Step 3: Update package.json with bin entry

```json
{
  "name": "raids-companion-server",
  "version": "0.1.0",
  "type": "module",
  "bin": {
    "raids": "./src/index.ts"
  },
  "scripts": {
    "dev": "bun run --watch src/index.ts",
    "start": "bun run src/index.ts",
    "test": "bun test"
  },
  "dependencies": {
    "ws": "^8.16.0"
  },
  "devDependencies": {
    "@types/bun": "latest",
    "@types/ws": "^8.5.10"
  }
}
```

### Step 4: Test CLI commands

```bash
cd companion-server

# Test help
bun run src/index.ts help

# Test install (creates/updates ~/.claude/settings.json)
bun run src/index.ts install

# Verify hooks installed
cat ~/.claude/settings.json

# Test uninstall
bun run src/index.ts uninstall

# Verify hooks removed
cat ~/.claude/settings.json
```

### Step 5: Commit

```bash
git add companion-server/
git commit -m "feat(server): add CLI commands for hook management

- raids install: adds hooks to ~/.claude/settings.json
- raids uninstall: removes hooks
- raids start: starts companion server
- Merges with existing settings, doesn't overwrite"
```

---

## Task 5: 3DS Top Screen UI

**Files:**
- Modify: `3ds-app/source/main.c`
- Create: `3ds-app/source/ui.h`
- Create: `3ds-app/source/ui.c`
- Create: `3ds-app/source/protocol.h`

### Step 1: Create protocol.h with shared types

```c
#ifndef PROTOCOL_H
#define PROTOCOL_H

typedef enum {
    STATE_IDLE = 0,
    STATE_WORKING,
    STATE_WAITING,
    STATE_ERROR,
    STATE_DONE
} AgentState;

typedef struct {
    char name[32];
    AgentState state;
    int progress;  // 0-100, -1 for indeterminate
    char message[128];
    char pending_command[256];
} Agent;

#define MAX_AGENTS 4

#endif // PROTOCOL_H
```

### Step 2: Create ui.h

```c
#ifndef UI_H
#define UI_H

#include <citro2d.h>
#include "protocol.h"

// Initialize UI resources
void ui_init(void);

// Cleanup UI resources
void ui_exit(void);

// Render top screen with agent dashboard
void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count, int selected);

// Render bottom screen with touch controls
void ui_render_bottom(C3D_RenderTarget* target, Agent* selected_agent, bool connected);

// Check if touch is in approve button (returns 1 if yes)
int ui_touch_approve(touchPosition touch);

// Check if touch is in deny button (returns 1 if yes)
int ui_touch_deny(touchPosition touch);

#endif // UI_H
```

### Step 3: Create ui.c

```c
#include "ui.h"
#include <stdio.h>
#include <string.h>

// Colors
static u32 clrBackground;
static u32 clrWhite;
static u32 clrGray;
static u32 clrGreen;
static u32 clrRed;
static u32 clrYellow;
static u32 clrBlue;

// Text buffers
static C2D_TextBuf textBuf;

// Screen dimensions
#define TOP_WIDTH 400
#define TOP_HEIGHT 240
#define BOT_WIDTH 320
#define BOT_HEIGHT 240

// Button dimensions (bottom screen)
#define BTN_APPROVE_X 20
#define BTN_APPROVE_Y 20
#define BTN_APPROVE_W 130
#define BTN_APPROVE_H 80

#define BTN_DENY_X 170
#define BTN_DENY_Y 20
#define BTN_DENY_W 130
#define BTN_DENY_H 80

void ui_init(void) {
    clrBackground = C2D_Color32(0x1a, 0x1a, 0x2e, 0xFF);
    clrWhite = C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF);
    clrGray = C2D_Color32(0x88, 0x88, 0x88, 0xFF);
    clrGreen = C2D_Color32(0x4C, 0xAF, 0x50, 0xFF);
    clrRed = C2D_Color32(0xF4, 0x43, 0x36, 0xFF);
    clrYellow = C2D_Color32(0xFF, 0xC1, 0x07, 0xFF);
    clrBlue = C2D_Color32(0x21, 0x96, 0xF3, 0xFF);

    textBuf = C2D_TextBufNew(1024);
}

void ui_exit(void) {
    C2D_TextBufDelete(textBuf);
}

static u32 state_to_color(AgentState state) {
    switch (state) {
        case STATE_WORKING: return clrBlue;
        case STATE_WAITING: return clrYellow;
        case STATE_ERROR:   return clrRed;
        case STATE_DONE:    return clrGreen;
        default:            return clrGray;
    }
}

static const char* state_to_string(AgentState state) {
    switch (state) {
        case STATE_WORKING: return "Working";
        case STATE_WAITING: return "Waiting";
        case STATE_ERROR:   return "Error";
        case STATE_DONE:    return "Done";
        default:            return "Idle";
    }
}

static void draw_progress_bar(float x, float y, float w, float h, int progress, u32 color) {
    // Background
    C2D_DrawRectSolid(x, y, 0, w, h, C2D_Color32(0x33, 0x33, 0x33, 0xFF));

    // Progress fill
    if (progress >= 0 && progress <= 100) {
        float fillW = (w * progress) / 100.0f;
        C2D_DrawRectSolid(x, y, 0, fillW, h, color);
    } else {
        // Indeterminate: pulse effect (simplified: just show 50%)
        C2D_DrawRectSolid(x, y, 0, w * 0.5f, h, color);
    }

    // Border
    C2D_DrawRectSolid(x, y, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y + h - 2, 0, w, 2, clrWhite);
    C2D_DrawRectSolid(x, y, 0, 2, h, clrWhite);
    C2D_DrawRectSolid(x + w - 2, y, 0, 2, h, clrWhite);
}

void ui_render_top(C3D_RenderTarget* target, Agent* agents, int agent_count, int selected) {
    C2D_TargetClear(target, clrBackground);
    C2D_SceneBegin(target);

    C2D_TextBufClear(textBuf);

    float row_height = 55.0f;
    float start_y = 10.0f;

    for (int i = 0; i < agent_count && i < MAX_AGENTS; i++) {
        Agent* agent = &agents[i];
        float y = start_y + (i * row_height);
        u32 stateColor = state_to_color(agent->state);

        // Selection highlight
        if (i == selected) {
            C2D_DrawRectSolid(0, y, 0, TOP_WIDTH, row_height - 5, C2D_Color32(0x2a, 0x2a, 0x4e, 0xFF));
        }

        // Agent name
        C2D_Text txtName;
        char nameBuf[64];
        snprintf(nameBuf, sizeof(nameBuf), "%s", agent->name);
        C2D_TextParse(&txtName, textBuf, nameBuf);
        C2D_TextOptimize(&txtName);
        C2D_DrawText(&txtName, C2D_WithColor, 10, y + 5, 0, 0.6f, 0.6f, clrWhite);

        // State label
        C2D_Text txtState;
        C2D_TextParse(&txtState, textBuf, state_to_string(agent->state));
        C2D_TextOptimize(&txtState);
        C2D_DrawText(&txtState, C2D_WithColor, 320, y + 5, 0, 0.5f, 0.5f, stateColor);

        // Progress bar
        draw_progress_bar(10, y + 25, 300, 12, agent->progress, stateColor);

        // Message
        C2D_Text txtMsg;
        char msgBuf[64];
        snprintf(msgBuf, sizeof(msgBuf), "%.50s", agent->message);
        C2D_TextParse(&txtMsg, textBuf, msgBuf);
        C2D_TextOptimize(&txtMsg);
        C2D_DrawText(&txtMsg, C2D_WithColor, 10, y + 40, 0, 0.45f, 0.45f, clrGray);

        // Separator line
        C2D_DrawRectSolid(0, y + row_height - 5, 0, TOP_WIDTH, 1, C2D_Color32(0x33, 0x33, 0x33, 0xFF));
    }

    // Title bar at bottom
    C2D_DrawRectSolid(0, TOP_HEIGHT - 20, 0, TOP_WIDTH, 20, C2D_Color32(0x0f, 0x0f, 0x1f, 0xFF));
    C2D_Text txtTitle;
    C2D_TextParse(&txtTitle, textBuf, "rAI3DS v0.1.0");
    C2D_TextOptimize(&txtTitle);
    C2D_DrawText(&txtTitle, C2D_WithColor, 160, TOP_HEIGHT - 17, 0, 0.5f, 0.5f, clrGray);
}

void ui_render_bottom(C3D_RenderTarget* target, Agent* selected_agent, bool connected) {
    C2D_TargetClear(target, clrBackground);
    C2D_SceneBegin(target);

    C2D_TextBufClear(textBuf);

    // Connection status
    if (!connected) {
        C2D_Text txtDisc;
        C2D_TextParse(&txtDisc, textBuf, "Connecting...");
        C2D_TextOptimize(&txtDisc);
        C2D_DrawText(&txtDisc, C2D_WithColor, 110, 110, 0, 0.8f, 0.8f, clrYellow);
        return;
    }

    // Approve button
    u32 approveColor = (selected_agent && selected_agent->state == STATE_WAITING) ? clrGreen : clrGray;
    C2D_DrawRectSolid(BTN_APPROVE_X, BTN_APPROVE_Y, 0, BTN_APPROVE_W, BTN_APPROVE_H, approveColor);
    C2D_Text txtApprove;
    C2D_TextParse(&txtApprove, textBuf, "APPROVE");
    C2D_TextOptimize(&txtApprove);
    C2D_DrawText(&txtApprove, C2D_WithColor, BTN_APPROVE_X + 25, BTN_APPROVE_Y + 30, 0, 0.8f, 0.8f, clrWhite);

    // Deny button
    u32 denyColor = (selected_agent && selected_agent->state == STATE_WAITING) ? clrRed : clrGray;
    C2D_DrawRectSolid(BTN_DENY_X, BTN_DENY_Y, 0, BTN_DENY_W, BTN_DENY_H, denyColor);
    C2D_Text txtDeny;
    C2D_TextParse(&txtDeny, textBuf, "DENY");
    C2D_TextOptimize(&txtDeny);
    C2D_DrawText(&txtDeny, C2D_WithColor, BTN_DENY_X + 40, BTN_DENY_Y + 30, 0, 0.8f, 0.8f, clrWhite);

    // Context area - show pending command
    if (selected_agent && selected_agent->pending_command[0] != '\0') {
        C2D_DrawRectSolid(10, 115, 0, 300, 50, C2D_Color32(0x2a, 0x2a, 0x4e, 0xFF));

        C2D_Text txtCmd;
        char cmdBuf[64];
        snprintf(cmdBuf, sizeof(cmdBuf), "%.55s", selected_agent->pending_command);
        C2D_TextParse(&txtCmd, textBuf, cmdBuf);
        C2D_TextOptimize(&txtCmd);
        C2D_DrawText(&txtCmd, C2D_WithColor, 15, 125, 0, 0.45f, 0.45f, clrWhite);
    }

    // Agent tabs at bottom
    float tabWidth = BOT_WIDTH / 4.0f;
    const char* agentNames[] = {"Claude", "Codex", "Gemini", "Cursor"};
    for (int i = 0; i < 4; i++) {
        float x = i * tabWidth;
        u32 tabColor = (i == 0) ? clrBlue : C2D_Color32(0x33, 0x33, 0x33, 0xFF);  // Only Claude active for MVP
        C2D_DrawRectSolid(x, BOT_HEIGHT - 30, 0, tabWidth - 2, 30, tabColor);

        C2D_Text txtTab;
        C2D_TextParse(&txtTab, textBuf, agentNames[i]);
        C2D_TextOptimize(&txtTab);
        C2D_DrawText(&txtTab, C2D_WithColor, x + 15, BOT_HEIGHT - 22, 0, 0.5f, 0.5f, clrWhite);
    }
}

int ui_touch_approve(touchPosition touch) {
    return (touch.px >= BTN_APPROVE_X && touch.px <= BTN_APPROVE_X + BTN_APPROVE_W &&
            touch.py >= BTN_APPROVE_Y && touch.py <= BTN_APPROVE_Y + BTN_APPROVE_H);
}

int ui_touch_deny(touchPosition touch) {
    return (touch.px >= BTN_DENY_X && touch.px <= BTN_DENY_X + BTN_DENY_W &&
            touch.py >= BTN_DENY_Y && touch.py <= BTN_DENY_Y + BTN_DENY_H);
}
```

### Step 4: Update main.c to use UI module

```c
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "protocol.h"

// Mock agents for testing UI
static Agent agents[MAX_AGENTS] = {
    { "CLAUDE CODE", STATE_WAITING, 75, "Waiting for approval", "rm -rf node_modules" },
    { "CODEX", STATE_WORKING, 50, "Running tests...", "" },
    { "GEMINI", STATE_IDLE, -1, "Ready", "" },
    { "CURSOR", STATE_DONE, 100, "Task completed!", "" }
};
static int selectedAgent = 0;
static bool connected = true;  // Mock connected state

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize UI
    ui_init();

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (ui_touch_approve(touch)) {
                printf("Approve pressed!\n");
                agents[selectedAgent].state = STATE_WORKING;
                agents[selectedAgent].message = "Approved - executing...";
            } else if (ui_touch_deny(touch)) {
                printf("Deny pressed!\n");
                agents[selectedAgent].state = STATE_IDLE;
                agents[selectedAgent].message = "Denied by user";
            }
        }

        // D-pad to switch agents
        if (kDown & KEY_DOWN) {
            selectedAgent = (selectedAgent + 1) % MAX_AGENTS;
        }
        if (kDown & KEY_UP) {
            selectedAgent = (selectedAgent - 1 + MAX_AGENTS) % MAX_AGENTS;
        }

        // Render
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, MAX_AGENTS, selectedAgent);
        ui_render_bottom(bottomScreen, &agents[selectedAgent], connected);
        C3D_FrameEnd(0);
    }

    // Cleanup
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
```

### Step 5: Build and test in Citra

```bash
# Build
docker compose run --rm 3ds-build

# Open in Citra
# macOS: open -a Citra 3ds-app/raids.3dsx
# Or drag raids.3dsx onto Citra
```

Expected: See 4-agent dashboard on top screen, approve/deny buttons on bottom, touch works

### Step 6: Commit

```bash
git add 3ds-app/source/
git commit -m "feat(3ds): add UI with agent dashboard and touch controls

- Top screen: multi-agent status display with progress bars
- Bottom screen: approve/deny buttons, agent tabs
- Touch input handling for buttons
- D-pad to switch between agents"
```

---

## Task 6: 3DS WebSocket Client

**Files:**
- Create: `3ds-app/source/network.h`
- Create: `3ds-app/source/network.c`
- Create: `3ds-app/source/cJSON.h`
- Create: `3ds-app/source/cJSON.c`
- Modify: `3ds-app/source/main.c`

### Step 1: Add cJSON library

Download cJSON from https://github.com/DaveGamble/cJSON and copy `cJSON.h` and `cJSON.c` to `3ds-app/source/`.

Or create minimal versions:

```bash
cd 3ds-app/source
curl -O https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h
curl -O https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c
```

### Step 2: Create network.h

```c
#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include "protocol.h"

// Initialize network (call once at startup)
bool network_init(void);

// Cleanup network
void network_exit(void);

// Connect to companion server
// Returns true if connection initiated (async)
bool network_connect(const char* host, int port);

// Disconnect from server
void network_disconnect(void);

// Check if connected
bool network_is_connected(void);

// Poll for incoming messages (call every frame)
// Updates agents array with received status
void network_poll(Agent* agents, int* agent_count);

// Send action to server
void network_send_action(const char* agent, const char* action);

// Send command to server
void network_send_command(const char* agent, const char* command);

#endif // NETWORK_H
```

### Step 3: Create network.c (simplified TCP, WebSocket frames)

```c
#include "network.h"
#include "cJSON.h"
#include <3ds.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define RECV_BUF_SIZE 4096
#define SEND_BUF_SIZE 1024

static int sock = -1;
static bool connected = false;
static bool ws_handshake_done = false;
static char recv_buf[RECV_BUF_SIZE];
static int recv_buf_len = 0;

// Simple WebSocket key (fixed for simplicity)
static const char* WS_KEY = "dGhlIHNhbXBsZSBub25jZQ==";

bool network_init(void) {
    // SOC service is needed for sockets on 3DS
    static u32* SOC_buffer = NULL;
    if (SOC_buffer == NULL) {
        SOC_buffer = (u32*)memalign(0x1000, 0x100000);
        if (SOC_buffer == NULL) {
            return false;
        }
        if (socInit(SOC_buffer, 0x100000) != 0) {
            free(SOC_buffer);
            SOC_buffer = NULL;
            return false;
        }
    }
    return true;
}

void network_exit(void) {
    network_disconnect();
    socExit();
}

bool network_connect(const char* host, int port) {
    if (sock >= 0) {
        network_disconnect();
    }

    struct hostent* server = gethostbyname(host);
    if (server == NULL) {
        printf("Failed to resolve host: %s\n", host);
        return false;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("Failed to create socket\n");
        return false;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Failed to connect\n");
        close(sock);
        sock = -1;
        return false;
    }

    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    // Send WebSocket handshake
    char handshake[512];
    snprintf(handshake, sizeof(handshake),
        "GET / HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        host, port, WS_KEY);

    send(sock, handshake, strlen(handshake), 0);

    connected = true;
    ws_handshake_done = false;
    recv_buf_len = 0;

    return true;
}

void network_disconnect(void) {
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    connected = false;
    ws_handshake_done = false;
}

bool network_is_connected(void) {
    return connected && ws_handshake_done;
}

static void parse_agent_status(const char* json, Agent* agents, int* agent_count) {
    cJSON* root = cJSON_Parse(json);
    if (root == NULL) return;

    cJSON* type = cJSON_GetObjectItem(root, "type");
    if (type == NULL || strcmp(type->valuestring, "agent_status") != 0) {
        cJSON_Delete(root);
        return;
    }

    cJSON* agent_name = cJSON_GetObjectItem(root, "agent");
    cJSON* state = cJSON_GetObjectItem(root, "state");
    cJSON* progress = cJSON_GetObjectItem(root, "progress");
    cJSON* message = cJSON_GetObjectItem(root, "message");
    cJSON* pending = cJSON_GetObjectItem(root, "pendingCommand");

    if (agent_name == NULL) {
        cJSON_Delete(root);
        return;
    }

    // Find or create agent slot
    int idx = -1;
    for (int i = 0; i < *agent_count; i++) {
        if (strcasecmp(agents[i].name, agent_name->valuestring) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && *agent_count < MAX_AGENTS) {
        idx = (*agent_count)++;
        strncpy(agents[idx].name, agent_name->valuestring, sizeof(agents[idx].name) - 1);
    }
    if (idx < 0) {
        cJSON_Delete(root);
        return;
    }

    // Update agent
    if (state) {
        const char* s = state->valuestring;
        if (strcmp(s, "working") == 0) agents[idx].state = STATE_WORKING;
        else if (strcmp(s, "waiting") == 0) agents[idx].state = STATE_WAITING;
        else if (strcmp(s, "error") == 0) agents[idx].state = STATE_ERROR;
        else if (strcmp(s, "done") == 0) agents[idx].state = STATE_DONE;
        else agents[idx].state = STATE_IDLE;
    }
    if (progress) agents[idx].progress = progress->valueint;
    if (message) strncpy(agents[idx].message, message->valuestring, sizeof(agents[idx].message) - 1);
    if (pending && pending->valuestring) {
        strncpy(agents[idx].pending_command, pending->valuestring, sizeof(agents[idx].pending_command) - 1);
    } else {
        agents[idx].pending_command[0] = '\0';
    }

    cJSON_Delete(root);
}

static void process_ws_frame(const unsigned char* data, int len, Agent* agents, int* agent_count) {
    if (len < 2) return;

    // Simple WebSocket frame parsing (assumes small, unfragmented, text frames)
    int opcode = data[0] & 0x0F;
    int payload_len = data[1] & 0x7F;
    int offset = 2;

    if (payload_len == 126) {
        if (len < 4) return;
        payload_len = (data[2] << 8) | data[3];
        offset = 4;
    }

    if (opcode == 0x01 && offset + payload_len <= len) {  // Text frame
        char json[RECV_BUF_SIZE];
        memcpy(json, data + offset, payload_len);
        json[payload_len] = '\0';
        parse_agent_status(json, agents, agent_count);
    }
}

void network_poll(Agent* agents, int* agent_count) {
    if (sock < 0) return;

    // Try to receive data
    int space = RECV_BUF_SIZE - recv_buf_len - 1;
    if (space > 0) {
        int n = recv(sock, recv_buf + recv_buf_len, space, 0);
        if (n > 0) {
            recv_buf_len += n;
            recv_buf[recv_buf_len] = '\0';
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Connection closed or error
            connected = false;
            return;
        }
    }

    // Check for WebSocket handshake response
    if (!ws_handshake_done) {
        char* end = strstr(recv_buf, "\r\n\r\n");
        if (end) {
            if (strstr(recv_buf, "101") != NULL) {
                ws_handshake_done = true;
                int handshake_len = (end - recv_buf) + 4;
                memmove(recv_buf, recv_buf + handshake_len, recv_buf_len - handshake_len);
                recv_buf_len -= handshake_len;
            } else {
                // Handshake failed
                network_disconnect();
                return;
            }
        }
        return;
    }

    // Process WebSocket frames
    while (recv_buf_len >= 2) {
        int payload_len = recv_buf[1] & 0x7F;
        int header_len = 2;
        if (payload_len == 126) header_len = 4;

        if (recv_buf_len < header_len) break;
        if (payload_len == 126) {
            payload_len = ((unsigned char)recv_buf[2] << 8) | (unsigned char)recv_buf[3];
        }

        int frame_len = header_len + payload_len;
        if (recv_buf_len < frame_len) break;

        process_ws_frame((unsigned char*)recv_buf, frame_len, agents, agent_count);

        memmove(recv_buf, recv_buf + frame_len, recv_buf_len - frame_len);
        recv_buf_len -= frame_len;
    }
}

static void send_ws_frame(const char* data) {
    if (sock < 0 || !ws_handshake_done) return;

    int len = strlen(data);
    unsigned char frame[SEND_BUF_SIZE];
    int offset = 0;

    frame[offset++] = 0x81;  // FIN + text opcode

    // Mask bit set (required from client), followed by length
    if (len < 126) {
        frame[offset++] = 0x80 | len;
    } else {
        frame[offset++] = 0x80 | 126;
        frame[offset++] = (len >> 8) & 0xFF;
        frame[offset++] = len & 0xFF;
    }

    // Masking key (just use zeros for simplicity, though spec says random)
    unsigned char mask[4] = {0x12, 0x34, 0x56, 0x78};
    memcpy(frame + offset, mask, 4);
    offset += 4;

    // Masked payload
    for (int i = 0; i < len; i++) {
        frame[offset++] = data[i] ^ mask[i % 4];
    }

    send(sock, frame, offset, 0);
}

void network_send_action(const char* agent, const char* action) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"type\":\"action\",\"agent\":\"%s\",\"action\":\"%s\"}",
        agent, action);
    send_ws_frame(json);
}

void network_send_command(const char* agent, const char* command) {
    char json[256];
    snprintf(json, sizeof(json),
        "{\"type\":\"command\",\"agent\":\"%s\",\"command\":\"%s\"}",
        agent, command);
    send_ws_frame(json);
}
```

### Step 4: Update main.c to use network

```c
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include <stdio.h>
#include "ui.h"
#include "protocol.h"
#include "network.h"

// Server configuration (change to your dev machine's IP)
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 3334

// Reconnection timing
#define RECONNECT_INTERVAL 120  // frames (~2 seconds at 60fps)

static Agent agents[MAX_AGENTS];
static int agent_count = 0;
static int selectedAgent = 0;
static int reconnect_timer = 0;

int main(int argc, char* argv[]) {
    // Initialize services
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);  // Debug console (temporary)
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets
    C3D_RenderTarget* topScreen = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bottomScreen = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Initialize UI and network
    ui_init();

    if (!network_init()) {
        printf("Network init failed!\n");
    } else {
        printf("Connecting to %s:%d...\n", SERVER_HOST, SERVER_PORT);
        network_connect(SERVER_HOST, SERVER_PORT);
    }

    // Initialize default agent
    strcpy(agents[0].name, "CLAUDE");
    agents[0].state = STATE_IDLE;
    agents[0].progress = -1;
    strcpy(agents[0].message, "Connecting...");
    agent_count = 1;

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if (kDown & KEY_START)
            break;

        // Network polling
        network_poll(agents, &agent_count);

        // Reconnection logic
        if (!network_is_connected()) {
            reconnect_timer++;
            if (reconnect_timer >= RECONNECT_INTERVAL) {
                reconnect_timer = 0;
                printf("Reconnecting...\n");
                network_connect(SERVER_HOST, SERVER_PORT);
            }
        } else {
            reconnect_timer = 0;
        }

        // Handle touch
        if (kDown & KEY_TOUCH) {
            touchPosition touch;
            hidTouchRead(&touch);

            if (ui_touch_approve(touch) && agents[selectedAgent].state == STATE_WAITING) {
                printf("Sending approve\n");
                network_send_action(agents[selectedAgent].name, "approve");
            } else if (ui_touch_deny(touch) && agents[selectedAgent].state == STATE_WAITING) {
                printf("Sending deny\n");
                network_send_action(agents[selectedAgent].name, "deny");
            }
        }

        // D-pad to switch agents
        if (kDown & KEY_DOWN && agent_count > 0) {
            selectedAgent = (selectedAgent + 1) % agent_count;
        }
        if (kDown & KEY_UP && agent_count > 0) {
            selectedAgent = (selectedAgent - 1 + agent_count) % agent_count;
        }

        // Render
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        ui_render_top(topScreen, agents, agent_count, selectedAgent);
        ui_render_bottom(bottomScreen,
            agent_count > 0 ? &agents[selectedAgent] : NULL,
            network_is_connected());
        C3D_FrameEnd(0);
    }

    // Cleanup
    network_exit();
    ui_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
```

### Step 5: Create config header for server address

Create `3ds-app/source/config.h`:

```c
#ifndef CONFIG_H
#define CONFIG_H

// Change this to your development machine's IP address
// Find it with: ifconfig | grep "inet " | grep -v 127.0.0.1
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 3334

#endif // CONFIG_H
```

Update main.c to use it:
```c
#include "config.h"
// ... remove the #define SERVER_HOST and SERVER_PORT lines
```

### Step 6: Build and test end-to-end

```bash
# Terminal 1: Start companion server
cd companion-server && bun run dev

# Terminal 2: Build 3DS app
docker compose run --rm 3ds-build

# Terminal 3: Open in Citra (or copy to real 3DS)
# Make sure to update config.h with correct IP first!
```

### Step 7: Commit

```bash
git add 3ds-app/source/
git commit -m "feat(3ds): add WebSocket client for server communication

- Connects to companion server over WiFi
- Receives agent status updates, updates UI
- Sends approve/deny actions on button press
- Auto-reconnects on disconnect
- Uses cJSON for message parsing"
```

---

## Task 7: Integration Testing & Polish

**Files:**
- Create: `scripts/test-e2e.sh`
- Modify: `companion-server/src/server.ts` (add test mode)
- Update: `README.md` (project root)

### Step 1: Create end-to-end test script

```bash
#!/bin/bash
# scripts/test-e2e.sh - End-to-end testing helper

set -e

echo "=== rAI3DS End-to-End Test ==="
echo ""

# Check dependencies
command -v bun >/dev/null 2>&1 || { echo "Error: bun not installed"; exit 1; }
command -v wscat >/dev/null 2>&1 || { echo "Installing wscat..."; bun add -g wscat; }

# Start server in background
echo "Starting companion server..."
cd companion-server
bun run src/index.ts &
SERVER_PID=$!
cd ..
sleep 2

# Test health endpoint
echo "Testing health endpoint..."
HEALTH=$(curl -s http://localhost:3333/health)
echo "Health: $HEALTH"

# Test hook endpoints
echo ""
echo "Testing pre-tool hook..."
curl -s -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}' | jq .

echo ""
echo "Testing waiting hook..."
curl -s -X POST http://localhost:3333/hook/waiting \
  -H 'Content-Type: application/json' \
  -d '{"command":"npm install express"}' | jq .

echo ""
echo "Testing post-tool hook..."
curl -s -X POST http://localhost:3333/hook/post-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}' | jq .

# Check final state
echo ""
echo "Final state:"
curl -s http://localhost:3333/health | jq .

# Cleanup
echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Tests complete ==="
echo ""
echo "To test with 3DS/Citra:"
echo "1. Update 3ds-app/source/config.h with your IP"
echo "2. Build: docker compose run --rm 3ds-build"
echo "3. Start server: cd companion-server && bun run dev"
echo "4. Run raids.3dsx in Citra or copy to 3DS"
```

### Step 2: Make script executable

```bash
chmod +x scripts/test-e2e.sh
```

### Step 3: Create project README.md

```markdown
# rAI3DS

A Nintendo 3DS homebrew application that serves as a dedicated companion device for AI coding agents.

**Status:** MVP in development

## Quick Start

### Prerequisites

- Docker and Docker Compose
- Bun (for local development)
- Nintendo 3DS with CFW or Citra emulator

### Companion Server

```bash
cd companion-server
bun install
bun run dev
```

The server runs on:
- HTTP :3333 (receives Claude Code hooks)
- WebSocket :3334 (3DS connection)

### 3DS App

```bash
# Build using Docker (no local devkitPro needed)
docker compose run --rm 3ds-build

# Output: 3ds-app/raids.3dsx
```

Before building, edit `3ds-app/source/config.h` with your dev machine's IP address.

### Install Claude Code Hooks

```bash
cd companion-server
bun run src/index.ts install
```

## Architecture

```
┌─────────────────┐                    ┌─────────────────┐                    ┌─────────────┐
│   Claude Code   │───── hooks ───────>│    Companion    │<==================>│     3DS     │
│   (terminal)    │<── tmux keys ──────│     Server      │      WiFi          │     App     │
└─────────────────┘                    └─────────────────┘                    └─────────────┘
```

## Testing

```bash
# Run automated tests
./scripts/test-e2e.sh

# Manual WebSocket testing
wscat -c ws://localhost:3334
```

## Project Structure

```
rAI3DS/
├── 3ds-app/           # Nintendo 3DS homebrew app (C/libctru)
├── companion-server/  # Bridge server (Bun/TypeScript)
├── plans/             # Design documents
└── scripts/           # Development utilities
```

## License

Open source (license TBD)
```

### Step 4: Run integration test

```bash
./scripts/test-e2e.sh
```

Expected: All curl commands succeed, state changes visible

### Step 5: Final commit

```bash
git add .
git commit -m "feat: complete MVP implementation

- Companion server with HTTP hooks and WebSocket
- 3DS app with agent dashboard and touch controls
- Claude Code adapter with tmux integration
- End-to-end test script
- Docker-based build system"
```

---

## Summary

The MVP is complete when:

1. ✅ `docker compose run --rm 3ds-build` produces `raids.3dsx`
2. ✅ `bun run dev` starts companion server on :3333/:3334
3. ✅ `raids install` adds hooks to `~/.claude/settings.json`
4. ✅ 3DS app connects to server and shows agent status
5. ✅ Approve/Deny buttons send actions that control Claude Code via tmux

## Next Steps (Post-MVP)

- Add character animations (sprite sheets)
- Support multiple agents (Codex, Gemini, Cursor adapters)
- Add sound effects
- Custom macros system
- Settings screen on 3DS
