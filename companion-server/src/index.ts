import { startHttpServer, setBroadcast, getAgentState } from "./server";
import { startWebSocketServer, broadcast } from "./websocket";

console.log("rAI3DS Companion Server starting...");

// Start HTTP server (for hooks)
startHttpServer();

// Start WebSocket server (for 3DS)
startWebSocketServer();

// Connect broadcast function
setBroadcast(broadcast);

console.log("Server ready. Waiting for hooks and 3DS connections...");
