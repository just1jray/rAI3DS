import type {
  PreToolHook,
  PostToolHook,
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
} from "./types";
import type { ClaudeAdapter } from "./adapters/claude";
import type { ServerWebSocket } from "bun";


const PORT = 3333;
const HOST = "0.0.0.0";

// In-memory state
const agentState: AgentStatus = {
  name: "claude",
  state: "idle",
  progress: -1,
  message: "Waiting for activity...",
  lastUpdate: Date.now(),
  contextPercent: 0,
};

// WebSocket clients (Bun native)
const wsClients = new Set<ServerWebSocket>();

let claudeAdapter: ClaudeAdapter | null = null;

// Auto-edit state (synced with 3DS)
let autoEditEnabled = false;

export function setClaudeAdapter(adapter: ClaudeAdapter) {
  claudeAdapter = adapter;
}

export function getAgentState(): AgentStatus {
  return agentState;
}

export function isAutoEditEnabled(): boolean {
  return autoEditEnabled;
}

function broadcastState() {
  const message: AgentStatusMessage = {
    type: "agent_status",
    agent: agentState.name,
    state: agentState.state,
    progress: agentState.progress,
    message: agentState.message,
    contextPercent: agentState.contextPercent,
    promptToolType: agentState.promptToolType,
    promptToolDetail: agentState.promptToolDetail,
    promptDescription: agentState.promptDescription,
    autoEdit: autoEditEnabled,
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

export function updateState(updates: Partial<AgentStatus>) {
  Object.assign(agentState, updates, { lastUpdate: Date.now() });
  broadcastState();
}


export function updateContextPercent(percent: number) {
  if (agentState.contextPercent === percent) return;
  agentState.contextPercent = percent;
  broadcastState();
}

export function getClientCount(): number {
  return wsClients.size;
}

// Handle incoming WebSocket messages from 3DS
async function handleWsMessage(msg: DSMessage) {
  console.log("[ws] Received:", JSON.stringify(msg));

  const isClaudeAgent = msg.agent.toLowerCase() === "claude";

  if (msg.type === "action" && isClaudeAgent) {
    // Send tmux keystrokes to Claude's permission prompt
    if (claudeAdapter) {
      try {
        switch (msg.action) {
          case "yes":
            await claudeAdapter.sendYes();
            break;
          case "always":
            await claudeAdapter.sendAlways();
            break;
          case "no":
            await claudeAdapter.sendNo();
            break;
          case "escape":
            await claudeAdapter.sendEscape();
            break;
        }
      } catch (e) {
        console.error("[ws] tmux keystroke error:", e);
      }
    }
  } else if (msg.type === "command" && isClaudeAgent && claudeAdapter) {
    await claudeAdapter.sendInput(msg.command);
  } else if (msg.type === "config" && isClaudeAgent) {
    if (msg.autoEdit !== undefined) {
      autoEditEnabled = msg.autoEdit;
      console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
      broadcastState();
    }
  }
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
          agent: agentState,
          autoEdit: autoEditEnabled,
          wsClients: wsClients.size,
        });
      }

      // Pre-tool hook: fire-and-forget status update
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          console.log(`[hook] pre-tool: ${body.tool}`);

          updateState({
            state: "working",
            progress: -1,
            message: `Tool: ${body.tool}`,
            promptToolType: undefined,
            promptToolDetail: undefined,
            promptDescription: undefined,
          });

          return Response.json({ action: "approve" });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Post-tool hook: fire-and-forget
      if (path === "/hook/post-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PostToolHook;
          console.log(`[hook] post-tool: ${body.tool}`);

          updateState({
            state: body.error ? "error" : "idle",
            progress: -1,
            message: body.error || `Done: ${body.tool}`,
            promptToolType: undefined,
            promptToolDetail: undefined,
            promptDescription: undefined,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      return Response.json({ error: "Not found" }, { status: 404 });
    },

    websocket: {
      open(ws) {
        console.log("[ws] 3DS client connected");
        wsClients.add(ws);
        // Send current state to newly connected client
        broadcastState();
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
