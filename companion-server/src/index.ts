import { startHttpServer, setBroadcast } from "./server";
import { startWebSocketServer, broadcast, setClaudeAdapter } from "./websocket";
import { createClaudeAdapter } from "./adapters/claude";

console.log("rAI3DS Companion Server starting...");

// Create Claude adapter
const claudeAdapter = createClaudeAdapter();
setClaudeAdapter(claudeAdapter);

// Start HTTP server (for hooks)
startHttpServer();

// Start WebSocket server (for 3DS)
startWebSocketServer();

// Connect broadcast function
setBroadcast(broadcast);

console.log("Server ready. Waiting for hooks and 3DS connections...");
console.log("Note: Claude Code session not started. Use CLI to start.");
