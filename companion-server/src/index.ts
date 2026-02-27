import { startServer } from "./server";
import { healthCheck } from "./session";

const HELP = `
rAI3DS Companion Server (Agent SDK)

Usage:
  raids [command]

Commands:
  start       Start the companion server (default)
  help        Show this help message

Examples:
  raids              # Start server
`;

async function main() {
  const command = process.argv[2] || "start";

  switch (command) {
    case "help":
    case "--help":
    case "-h":
      console.log(HELP);
      process.exit(0);

    case "start":
      break;

    default:
      console.error(`Unknown command: ${command}`);
      console.log(HELP);
      process.exit(1);
  }

  // Start server (HTTP + WebSocket on port 3333)
  console.log("rAI3DS Companion Server starting (Agent SDK mode)...");

  startServer();

  // Health check: every 30s, check for dead SDK sessions
  setInterval(() => {
    healthCheck().catch((e) => console.error("[health] Error:", e));
  }, 30_000);

  console.log("Server ready. Spawn agents from the 3DS or connect via WebSocket.");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
