import type {
  PreToolHook,
  PostToolHook,
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
  SpawnResultMessage,
  SessionStartHook,
  SessionEndHook,
  StopHook,
  UserPromptHook,
} from "./types";
import type { ServerWebSocket } from "bun";
import {
  resolveSlot,
  getSession,
  getAllSessions,
  getAdapterForSlot,
  spawnSession,
  killSession,
  findFreeSlot,
  linkSession,
  touchSession,
  MAX_SLOTS,
} from "./session";
import { $ } from "bun";

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
    active: i === 0, // Only slot 0 starts active
  });
}

// WebSocket clients (Bun native)
const wsClients = new Set<ServerWebSocket>();

// Auto-edit state (synced with 3DS)
let autoEditEnabled = false;

// Per-slot hook-provided tool data
const pendingToolData = new Map<number, { toolType: string; toolDetail: string; description: string }>();

export function getAgentState(slot: number = 0): AgentStatus {
  return agentStates[slot];
}

export function getAgentStates(): AgentStatus[] {
  return agentStates;
}

export function isAutoEditEnabled(): boolean {
  return autoEditEnabled;
}

export function getPendingToolData(slot: number = 0) {
  return pendingToolData.get(slot) ?? null;
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

async function doSpawn(slot: number): Promise<void> {
  console.log(`[ws] Spawn requested for slot ${slot}`);
  const success = await spawnSession(slot);
  if (success) {
    agentStates[slot].active = true;
    agentStates[slot].name = `claude-${slot}`;
    agentStates[slot].state = "idle";
    agentStates[slot].message = "Spawning...";
  }
  broadcastSpawnResult(slot, success, success ? undefined : "Failed to create tmux session");
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
    try {
      switch (msg.action) {
        case "yes":    await adapter.sendYes();    break;
        case "always": await adapter.sendAlways(); break;
        case "no":     await adapter.sendNo();     break;
        case "escape": await adapter.sendEscape(); break;
      }
    } catch (e) {
      console.error("[ws] tmux keystroke error:", e);
    }
  } else if (msg.type === "command" && adapter) {
    await adapter.sendInput(msg.command);
  } else if (msg.type === "config") {
    if (msg.autoEdit !== undefined) {
      autoEditEnabled = msg.autoEdit;
      console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
      const session = getSession(targetSlot);
      if (session) {
        const label = autoEditEnabled ? "ON" : "OFF";
        $`tmux display-message -t ${session.tmuxPaneId} "[rAI3DS] Auto-edit: ${label}"`.quiet().catch(() => {});
      }
      broadcastAllSlots();
    }
  }
}

function extractToolDetail(toolInput: Record<string, unknown>): string {
  const keys = ["command", "file_path", "pattern", "query", "url"] as const;
  for (const key of keys) {
    if (typeof toolInput[key] === "string") return toolInput[key] as string;
  }
  const firstVal = Object.values(toolInput)[0];
  return typeof firstVal === "string" ? firstVal : "";
}

export function startServer() {
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
            tmux: s.tmuxPaneId,
            status: s.status,
            sessionId: s.claudeSessionId,
          })),
        });
      }

      // Pre-tool hook
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          const slot = resolveSlot(body.session_id);
          const toolName = body.tool_name || body.tool || "Unknown";
          console.log(`[hook] pre-tool (slot ${slot}): ${toolName}`);

          const toolDetail = body.tool_input ? extractToolDetail(body.tool_input) : "";
          const description = typeof body.tool_input?.description === "string"
            ? body.tool_input.description
            : "";

          pendingToolData.set(slot, { toolType: toolName, toolDetail, description });
          touchSession(slot);

          updateState(slot, {
            state: "working",
            progress: -1,
            message: `Tool: ${toolName}`,
            promptToolType: toolName,
            promptToolDetail: toolDetail,
            promptDescription: description,
          });

          return Response.json({ action: "approve" });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Post-tool hook
      if (path === "/hook/post-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PostToolHook;
          const slot = resolveSlot(body.session_id);
          const toolName = body.tool_name || body.tool || "Unknown";
          console.log(`[hook] post-tool (slot ${slot}): ${toolName}`);

          pendingToolData.delete(slot);
          touchSession(slot);

          updateState(slot, {
            state: body.error ? "error" : "idle",
            progress: -1,
            message: body.error || `Done: ${toolName}`,
            promptToolType: undefined,
            promptToolDetail: undefined,
            promptDescription: undefined,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Session lifecycle hooks
      if (path === "/hook/session-start" && req.method === "POST") {
        try {
          const body = (await req.json()) as SessionStartHook;
          if (body.session_id) {
            const slot = resolveSlot(body.session_id);
            console.log(`[hook] session-start (slot ${slot}): ${body.session_id}`);
            touchSession(slot);
            updateState(slot, { state: "idle", message: "Session started" });
          }
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      if (path === "/hook/session-end" && req.method === "POST") {
        try {
          const body = (await req.json()) as SessionEndHook;
          if (body.session_id) {
            const slot = resolveSlot(body.session_id);
            console.log(`[hook] session-end (slot ${slot}): ${body.session_id}`);
            updateState(slot, { state: "done", message: "Session ended", active: false });
          }
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      if (path === "/hook/stop" && req.method === "POST") {
        try {
          const body = (await req.json()) as StopHook;
          const slot = resolveSlot(body.session_id);
          console.log(`[hook] stop (slot ${slot})`);
          touchSession(slot);
          updateState(slot, { state: "idle", message: "Stopped" });
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      if (path === "/hook/user-prompt" && req.method === "POST") {
        try {
          const body = (await req.json()) as UserPromptHook;
          const slot = resolveSlot(body.session_id);
          console.log(`[hook] user-prompt (slot ${slot})`);
          touchSession(slot);
          updateState(slot, { state: "working", message: "Processing prompt..." });
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
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
