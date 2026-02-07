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
