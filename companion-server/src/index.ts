import { startHttpServer, setBroadcast } from "./server";
import { startWebSocketServer, broadcast, setClaudeAdapter } from "./websocket";
import { createClaudeAdapter } from "./adapters/claude";
import { installHooks, uninstallHooks } from "./hooks";
import { startContextTracker } from "./context";

const HELP = `
rAI3DS Companion Server

Usage:
  raids [command]

Commands:
  start       Start the companion server (default)
  install     Install Claude Code hooks
  uninstall   Remove Claude Code hooks
  help        Show this help message

Examples:
  raids              # Start server
  raids install      # Install hooks, then start server
  raids uninstall    # Remove hooks
`;

async function main() {
  const command = process.argv[2] || "start";

  switch (command) {
    case "help":
    case "--help":
    case "-h":
      console.log(HELP);
      process.exit(0);

    case "install":
      const installed = await installHooks();
      if (!installed) process.exit(1);
      // Fall through to start
      break;

    case "uninstall":
      const uninstalled = await uninstallHooks();
      process.exit(uninstalled ? 0 : 1);

    case "start":
      break;

    default:
      console.error(`Unknown command: ${command}`);
      console.log(HELP);
      process.exit(1);
  }

  // Start servers
  console.log("rAI3DS Companion Server starting...");

  const claudeAdapter = createClaudeAdapter();
  setClaudeAdapter(claudeAdapter);

  startHttpServer();
  startWebSocketServer();
  setBroadcast(broadcast);
  startContextTracker(10_000);

  console.log("Server ready. Waiting for hooks and 3DS connections...");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
