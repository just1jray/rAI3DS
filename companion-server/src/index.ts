import { startServer } from "./server";
import { autoSpawnDefaultSession, healthCheck } from "./session";

const HELP = `
rAI3DS Companion Server

Usage:
  raids [command]

Commands:
  start       Start the companion server (default)
  help        Show this help message

Examples:
  raids              # Start server
`;

function main() {
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

  console.log("rAI3DS Companion Server starting...");

  startServer();
  autoSpawnDefaultSession();

  // Health check: every 30s, check for dead subprocesses
  setInterval(() => {
    healthCheck();
  }, 30_000);

  console.log("Server ready. Waiting for CLI and 3DS connections...");
}

main();
