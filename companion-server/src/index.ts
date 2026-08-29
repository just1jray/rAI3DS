import { startServer } from "./server";
import { installHooks, uninstallHooks } from "./hooks";
import { startContextTracker } from "./context";

const HELP = `
rAI3DS Companion Server

Turn your Nintendo 3DS into a permission remote for Claude Code.
No tmux required — sessions register via hooks, permissions via HTTP.

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

How it works:
  1. Run 'raids install' to set up HTTP hooks in ~/.claude/settings.json
  2. Run 'raids' (or 'raids start') to start the companion server
  3. Open Claude Code — the session registers automatically via SessionStart hook
  4. When Claude needs permission, the PermissionRequest hook holds the request
  5. Your 3DS sees STATE_WAITING and can approve (A), always (Y), or deny (B)
  6. The held HTTP response resolves with allow/deny, Claude continues

No tmux session required. No screen scraping. Real permission control.
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

  // Start server (HTTP + WebSocket on port 3333)
  console.log("rAI3DS Companion Server starting...");
  console.log("");
  console.log("Architecture:");
  console.log("  Claude Code ──[HTTP hooks]──> Companion Server <══[WebSocket]══> 3DS");
  console.log("");
  console.log("Key endpoints:");
  console.log("  POST /hook/permission-request  — Holds until 3DS responds (allow/deny)");
  console.log("  POST /hook/session-start       — Registers Claude session (no tmux)");
  console.log("  POST /hook/session-end         — Cleans up session");
  console.log("  GET  /health                   — Server status");
  console.log("  WS   /                         — 3DS connection");
  console.log("");

  startServer();
  startContextTracker(10_000);

  console.log("Server ready. Open Claude Code — the session will register via hooks.");
  console.log("No tmux session required.");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
