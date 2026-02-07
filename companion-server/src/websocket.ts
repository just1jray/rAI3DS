import { WebSocketServer, WebSocket } from "ws";
import type { AgentStatus, DSMessage, AgentStatusMessage } from "./types";
import type { ClaudeAdapter } from "./adapters/claude";

const WS_PORT = 3334;
const HOST = "0.0.0.0"; // Listen on all interfaces so 3DS on LAN can connect

let wss: WebSocketServer;
const clients: Set<WebSocket> = new Set();
let claudeAdapter: ClaudeAdapter | null = null;

export function setClaudeAdapter(adapter: ClaudeAdapter) {
  claudeAdapter = adapter;
}

export function startWebSocketServer() {
  wss = new WebSocketServer({ host: HOST, port: WS_PORT });

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

  console.log(`WebSocket server listening on ws://${HOST}:${WS_PORT}`);
  return wss;
}

async function handleMessage(msg: DSMessage) {
  console.log("[ws] Received:", msg);

  const isClaudeAgent = msg.agent.toLowerCase() === "claude";

  if (msg.type === "action" && isClaudeAgent && claudeAdapter) {
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
  } else if (msg.type === "command" && isClaudeAgent && claudeAdapter) {
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
    contextPercent: status.contextPercent,
    promptToolType: status.promptToolType,
    promptToolDetail: status.promptToolDetail,
    promptDescription: status.promptDescription,
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
