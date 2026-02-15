import type {
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
  SpawnResultMessage,
  WSData,
} from "./types";
import type { ServerWebSocket } from "bun";
import {
  handleCLIOpen,
  handleCLIMessage,
  handleCLIClose,
  sendInterrupt,
  sendUserMessage,
  resolvePermission,
} from "./cli-handler";
import {
  spawnSession,
  findFreeSlot,
  getAllSessions,
} from "./session";

const PORT = 3333;
const HOST = "0.0.0.0";
const MAX_SLOTS = 4;

// In-memory state — one per slot
const agentStates: AgentStatus[] = [];
for (let i = 0; i < MAX_SLOTS; i++) {
  agentStates.push({
    name: i === 0 ? "claude" : `agent-${i}`,
    state: "idle",
    progress: -1,
    message: "Waiting for activity...",
    lastUpdate: Date.now(),
    contextPercent: 0,
    slot: i,
    active: false,
  });
}

// WebSocket clients — 3DS connections only
const wsClients = new Set<ServerWebSocket<WSData>>();

// Auto-edit state (synced with 3DS)
let autoEditEnabled = false;

export function getAgentState(slot: number = 0): AgentStatus {
  return agentStates[slot];
}

export function getAgentStates(): AgentStatus[] {
  return agentStates;
}

export function isAutoEditEnabled(): boolean {
  return autoEditEnabled;
}

function broadcastSlotState(slot: number) {
  const state = agentStates[slot];
  const message: AgentStatusMessage = {
    type: "agent_status",
    agent: state.name,
    state: state.state,
    progress: state.progress,
    message: state.message,
    contextPercent: state.contextPercent,
    promptToolType: state.promptToolType,
    promptToolDetail: state.promptToolDetail,
    promptDescription: state.promptDescription,
    autoEdit: autoEditEnabled,
    slot: state.slot,
    active: state.active,
  };

  const data = JSON.stringify(message);
  for (const client of wsClients) {
    try {
      client.send(data);
    } catch {
      wsClients.delete(client);
    }
  }
}

function broadcastAllSlots() {
  for (let i = 0; i < MAX_SLOTS; i++) {
    if (agentStates[i].active) {
      broadcastSlotState(i);
    }
  }
}

function broadcastSpawnResult(slot: number, success: boolean, error?: string) {
  const message: SpawnResultMessage = {
    type: "spawn_result",
    slot,
    success,
    error,
  };

  const data = JSON.stringify(message);
  for (const client of wsClients) {
    try {
      client.send(data);
    } catch {
      wsClients.delete(client);
    }
  }
}

export function updateState(slot: number, updates: Partial<AgentStatus>) {
  Object.assign(agentStates[slot], updates, { lastUpdate: Date.now() });
  broadcastSlotState(slot);
}

export function getClientCount(): number {
  return wsClients.size;
}

// Handle incoming WebSocket messages from 3DS
function handleWsMessage(msg: DSMessage) {
  console.log("[ws] Received:", JSON.stringify(msg));

  switch (msg.type) {
    case "action": {
      const slot = msg.slot ?? 0;
      if (msg.action === "escape") {
        sendInterrupt(slot);
      } else {
        resolvePermission(slot, msg.action);
      }
      break;
    }

    case "command": {
      const slot = (msg as any).slot ?? 0;
      if (msg.command === "spawn") {
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
        console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
        broadcastAllSlots();
      }
      break;
    }
  }
}

export function startServer() {
  const server = Bun.serve<WSData>({
    hostname: HOST,
    port: PORT,

    fetch(req, server) {
      const url = new URL(req.url);

      // CLI WebSocket: /ws/cli/:slot
      const cliMatch = url.pathname.match(/^\/ws\/cli\/(\d+)$/);
      if (cliMatch) {
        const slot = parseInt(cliMatch[1]);
        if (slot < 0 || slot >= MAX_SLOTS) {
          return new Response("Invalid slot", { status: 400 });
        }
        const upgraded = server.upgrade(req, { data: { type: "cli" as const, slot } });
        return upgraded ? undefined : new Response("Upgrade failed", { status: 500 });
      }

      // Health endpoint
      if (url.pathname === "/health" && req.method === "GET") {
        return Response.json({
          status: "ok",
          agents: agentStates,
          autoEdit: autoEditEnabled,
          wsClients: wsClients.size,
          sessions: getAllSessions().map(s => ({
            slot: s.slot,
            pid: s.pid,
            status: s.status,
          })),
        });
      }

      // 3DS WebSocket: everything else that wants an upgrade
      if (req.headers.get("upgrade")?.toLowerCase() === "websocket") {
        const upgraded = server.upgrade(req, { data: { type: "3ds" as const } });
        return upgraded ? undefined : new Response("Upgrade failed", { status: 500 });
      }

      return new Response("rAI3DS companion server", { status: 200 });
    },

    websocket: {
      open(ws) {
        if (ws.data.type === "cli") {
          handleCLIOpen(ws, ws.data.slot!);
        } else {
          console.log("[ws] 3DS client connected");
          wsClients.add(ws);
          broadcastAllSlots();
        }
      },

      message(ws, data) {
        const text = typeof data === "string" ? data : Buffer.from(data).toString();
        if (ws.data.type === "cli") {
          handleCLIMessage(ws, text);
        } else {
          try {
            const msg = JSON.parse(text) as DSMessage;
            handleWsMessage(msg);
          } catch (e) {
            console.error("[ws] Invalid 3DS message:", e);
          }
        }
      },

      close(ws) {
        if (ws.data.type === "cli") {
          handleCLIClose(ws, ws.data.slot!);
        } else {
          console.log("[ws] 3DS client disconnected");
          wsClients.delete(ws);
        }
      },
    },
  });

  console.log(`Server listening on http://${HOST}:${PORT} (HTTP + WebSocket)`);
  return server;
}
