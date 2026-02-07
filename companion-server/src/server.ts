import type { PreToolHook, PostToolHook, AgentStatus } from "./types";

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

      // Pre-tool hook: instant no-op (scraper handles prompt detection)
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          console.log(`[hook] pre-tool: ${body.tool}`);

          updateState({
            state: "working",
            progress: -1,
            message: `Running: ${body.tool}`,
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
