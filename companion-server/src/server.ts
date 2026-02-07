import type { PreToolHook, PostToolHook, AgentStatus } from "./types";
import { isTmuxAvailable } from "./scraper";

const HTTP_PORT = 3333;

// In-memory state
const agentState: AgentStatus = {
  name: "claude",
  state: "idle",
  progress: -1,
  message: "Waiting for activity...",
  lastUpdate: Date.now(),
  contextPercent: 0,
};

// Broadcast function (will connect to WebSocket)
let broadcast: (status: AgentStatus) => void = () => {};

export function setBroadcast(fn: (status: AgentStatus) => void) {
  broadcast = fn;
}

export function getAgentState(): AgentStatus {
  return agentState;
}

export function updateState(updates: Partial<AgentStatus>) {
  Object.assign(agentState, updates, { lastUpdate: Date.now() });
  broadcast(agentState);
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

// Context percent updater (called by context tracker)
export function updateContextPercent(percent: number) {
  if (agentState.contextPercent === percent) return;
  agentState.contextPercent = percent;
  broadcast(agentState);
}

const HOST = "0.0.0.0";

export function startHttpServer() {
  const server = Bun.serve({
    hostname: HOST,
    port: HTTP_PORT,
    async fetch(req) {
      const url = new URL(req.url);
      const path = url.pathname;

      // Health check
      if (path === "/health" && req.method === "GET") {
        return Response.json({ status: "ok", agent: agentState });
      }

      // Pre-tool hook
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          console.log(`[hook] pre-tool: ${body.tool} (tmux=${isTmuxAvailable()})`);

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
  });

  console.log(`HTTP server listening on http://${HOST}:${HTTP_PORT}`);
  return server;
}
