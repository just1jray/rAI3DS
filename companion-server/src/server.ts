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
