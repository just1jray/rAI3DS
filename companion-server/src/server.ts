import type {
  PreToolHook,
  PostToolHook,
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
} from "./types";
import type { ClaudeAdapter } from "./adapters/claude";
import type { ServerWebSocket } from "bun";
import { isTmuxAvailable } from "./scraper";

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
const AUTO_EDIT_TOOLS = ["edit", "write", "notebookedit"];

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

// Pending tool approval — used when tmux is NOT available (hook-blocking fallback)
let pendingToolResolve: ((action: "approve" | "deny") => void) | null = null;
const TOOL_TIMEOUT_MS = 5 * 60 * 1000; // 5 minutes

export function resolveToolAction(action: "approve" | "deny") {
  if (pendingToolResolve) {
    console.log(`[server] Tool action resolved: ${action}`);
    pendingToolResolve(action);
    pendingToolResolve = null;
  }
}

export function hasPendingTool(): boolean {
  return pendingToolResolve !== null;
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
    const hookAction =
      msg.action === "no" || msg.action === "escape" ? "deny" : "approve";

    // Resolve blocking hook if one is pending (no-tmux fallback)
    if (hasPendingTool()) {
      resolveToolAction(hookAction);
    }

    // Also try tmux keystrokes (works when Claude is in tmux)
    if (claudeAdapter && isTmuxAvailable()) {
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
    // Handle config messages (auto-edit toggle)
    if (msg.autoEdit !== undefined) {
      autoEditEnabled = msg.autoEdit;
      console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
      broadcastState(); // Echo back to all clients
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

      // Pre-tool hook
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          const toolLower = body.tool.toLowerCase();
          console.log(
            `[hook] pre-tool: ${body.tool} (tmux=${isTmuxAvailable()}, autoEdit=${autoEditEnabled})`
          );

          // Auto-edit: auto-approve Edit/Write/NotebookEdit immediately
          if (autoEditEnabled && AUTO_EDIT_TOOLS.includes(toolLower)) {
            console.log(`[hook] Auto-approving edit tool: ${body.tool}`);
            updateState({
              state: "working",
              progress: -1,
              message: `Auto-approved: ${body.tool}`,
              promptToolType: undefined,
              promptToolDetail: undefined,
              promptDescription: undefined,
            });
            return Response.json({ action: "approve" });
          }

          // Set prompt info from hook tool name
          updateState({
            state: "waiting",
            progress: -1,
            message: `Approve? ${body.tool}`,
            promptToolType: body.tool,
            promptToolDetail: "",
            promptDescription: "",
          });

          // If tmux is available, respond immediately — scraper handles the rest
          if (isTmuxAvailable()) {
            return Response.json({ action: "approve" });
          }

          // No tmux: block and wait for 3DS user to approve/deny
          const action = await new Promise<"approve" | "deny">((resolve) => {
            pendingToolResolve = resolve;
            setTimeout(() => {
              if (pendingToolResolve === resolve) {
                console.log("[hook] Tool approval timed out, auto-approving");
                pendingToolResolve = null;
                resolve("approve");
              }
            }, TOOL_TIMEOUT_MS);
          });

          if (action === "approve") {
            updateState({
              state: "working",
              progress: -1,
              message: `Running: ${body.tool}`,
              promptToolType: undefined,
              promptToolDetail: undefined,
              promptDescription: undefined,
            });
          } else {
            updateState({
              state: "idle",
              progress: -1,
              message: `Denied: ${body.tool}`,
              promptToolType: undefined,
              promptToolDetail: undefined,
              promptDescription: undefined,
            });
          }

          return Response.json({ action });
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
