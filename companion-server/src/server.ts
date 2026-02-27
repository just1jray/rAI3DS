import type {
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
  SpawnResultMessage,
} from "./types";
import type { ServerWebSocket } from "bun";
import type { SDKAdapterCallbacks } from "./adapters/sdk";
import {
  getSession,
  getAllSessions,
  getAdapterForSlot,
  spawnSession,
  findFreeSlot,
  linkSession,
  touchSession,
  setAdapterCallbacks,
  MAX_SLOTS,
} from "./session";

const PORT = 3333;
const HOST = "0.0.0.0";

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

// WebSocket clients (Bun native)
const wsClients = new Set<ServerWebSocket>();

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

function broadcast(data: string) {
  for (const client of wsClients) {
    try {
      client.send(data);
    } catch {
      wsClients.delete(client);
    }
  }
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
  broadcast(JSON.stringify(message));
}

function broadcastAllSlots() {
  for (let i = 0; i < MAX_SLOTS; i++) {
    broadcastSlotState(i);
  }
}

function broadcastSpawnResult(slot: number, success: boolean, error?: string) {
  const message: SpawnResultMessage = { type: "spawn_result", slot, success, error };
  broadcast(JSON.stringify(message));
}

export function updateState(slot: number, updates: Partial<AgentStatus>) {
  Object.assign(agentStates[slot], updates, { lastUpdate: Date.now() });
  broadcastSlotState(slot);
}

export function updateContextPercent(percent: number, slot: number = 0) {
  if (agentStates[slot].contextPercent === percent) return;
  agentStates[slot].contextPercent = percent;
  broadcastSlotState(slot);
}

export function getClientCount(): number {
  return wsClients.size;
}

// SDK adapter callbacks — wired to broadcast state to 3DS
const sdkCallbacks: SDKAdapterCallbacks = {
  onStateChange(slot, state, message, toolInfo) {
    const updates: Partial<AgentStatus> = {
      state,
      message,
      progress: state === "working" ? -1 : 0,
    };
    if (toolInfo) {
      updates.promptToolType = toolInfo.toolType;
      updates.promptToolDetail = toolInfo.toolDetail;
      updates.promptDescription = toolInfo.description;
    } else if (state !== "waiting") {
      // Clear tool info when not waiting and no tool info provided
      updates.promptToolType = undefined;
      updates.promptToolDetail = undefined;
      updates.promptDescription = undefined;
    }
    touchSession(slot);
    updateState(slot, updates);
  },

  onPermissionRequest(slot, toolName, toolInput, toolUseID) {
    // Store the pending toolUseID so we can route 3DS responses
    agentStates[slot].pendingToolUseID = toolUseID;
    console.log(`[sdk] Permission request (slot ${slot}): ${toolName} [${toolUseID}]`);
  },

  onContextUpdate(slot, percent) {
    updateContextPercent(percent, slot);
  },

  onSessionReady(slot, sessionId) {
    linkSession(sessionId, slot);
    agentStates[slot].sdkSessionId = sessionId;
    agentStates[slot].active = true;
    console.log(`[sdk] Session ready (slot ${slot}): ${sessionId}`);
    broadcastSlotState(slot);
  },

  isAutoEditEnabled() {
    return autoEditEnabled;
  },
};

async function doSpawn(slot: number): Promise<void> {
  console.log(`[ws] Spawn requested for slot ${slot}`);
  const success = await spawnSession(slot);
  if (success) {
    agentStates[slot].active = true;
    agentStates[slot].name = `claude-${slot}`;
    agentStates[slot].state = "idle";
    agentStates[slot].message = "Spawning...";
  }
  broadcastSpawnResult(slot, success, success ? undefined : "Failed to create SDK session");
  broadcastSlotState(slot);
}

// Handle incoming WebSocket messages from 3DS
async function handleWsMessage(msg: DSMessage) {
  console.log("[ws] Received:", JSON.stringify(msg));

  if (msg.type === "spawn_request") {
    const slot = msg.slot ?? findFreeSlot();
    if (slot === undefined) {
      console.log("[ws] No free slots for spawn");
      broadcastSpawnResult(-1, false, "No free slots");
      return;
    }
    await doSpawn(slot);
    return;
  }

  // Determine target slot
  const targetSlot = "slot" in msg ? (msg.slot ?? 0) : 0;
  const adapter = getAdapterForSlot(targetSlot);

  if (msg.type === "action" && adapter) {
    const pendingToolUseID = agentStates[targetSlot].pendingToolUseID || "";

    if (msg.action === "escape") {
      // Escape = interrupt the current query
      adapter.interrupt();
    } else {
      // yes/always/no → resolve via SDK adapter
      adapter.resolvePermission(pendingToolUseID, msg.action);
      agentStates[targetSlot].pendingToolUseID = undefined;
    }
  } else if (msg.type === "command" && adapter) {
    // Send text input to the agent as a follow-up prompt
    adapter.sendPrompt(msg.command);
  } else if (msg.type === "config") {
    if (msg.autoEdit !== undefined) {
      autoEditEnabled = msg.autoEdit;
      console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
      broadcastAllSlots();
    }
  }
}

export function startServer() {
  // Wire up SDK adapter callbacks before anything else
  setAdapterCallbacks(sdkCallbacks);

  const server = Bun.serve({
    hostname: HOST,
    port: PORT,

    async fetch(req, server) {
      // WebSocket upgrade
      if (req.headers.get("upgrade")?.toLowerCase() === "websocket") {
        if (server.upgrade(req)) {
          return undefined;
        }
        return new Response("WebSocket upgrade failed", { status: 400 });
      }

      const url = new URL(req.url);
      const path = url.pathname;

      // Health check
      if (path === "/health" && req.method === "GET") {
        return Response.json({
          status: "ok",
          agents: agentStates,
          autoEdit: autoEditEnabled,
          wsClients: wsClients.size,
          sessions: getAllSessions().map(s => ({
            slot: s.slot,
            status: s.status,
            sdkSessionId: s.sdkSessionId,
          })),
        });
      }

      return Response.json({ error: "Not found" }, { status: 404 });
    },

    websocket: {
      open(ws) {
        console.log("[ws] 3DS client connected");
        wsClients.add(ws);
        // Send current state of all slots to the new client
        broadcastAllSlots();
      },

      message(ws, data) {
        try {
          const text =
            typeof data === "string" ? data : new TextDecoder().decode(data);
          const msg = JSON.parse(text) as DSMessage;
          handleWsMessage(msg);
        } catch (e) {
          console.error("[ws] Invalid message:", e);
        }
      },

      close(ws) {
        console.log("[ws] 3DS client disconnected");
        wsClients.delete(ws);
      },
    },
  });

  console.log(
    `Server listening on http://${HOST}:${PORT} (HTTP + WebSocket)`
  );
  return server;
}
