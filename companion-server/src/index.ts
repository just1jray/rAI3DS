import { startServer, setClaudeAdapter, updateState } from "./server";
import { createClaudeAdapter } from "./adapters/claude";
import { installHooks, uninstallHooks } from "./hooks";
import { startContextTracker } from "./context";
import { startScraper } from "./scraper";

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

  // Start server (HTTP + WebSocket on port 3333)
  console.log("rAI3DS Companion Server starting...");

  const claudeAdapter = createClaudeAdapter();
  setClaudeAdapter(claudeAdapter);

  startServer();
  startContextTracker(10_000);

  // Start tmux screen scraper to detect permission prompts
  startScraper({
    onPromptAppeared(prompt) {
      console.log(
        `[scraper] Prompt appeared: ${prompt.toolType} — ${prompt.toolDetail}`
      );
      updateState({
        state: "waiting",
        message: `${prompt.toolType}: ${prompt.toolDetail}`,
        promptToolType: prompt.toolType,
        promptToolDetail: prompt.toolDetail,
        promptDescription: prompt.description,
      });
    },
    onPromptDisappeared() {
      console.log("[scraper] Prompt disappeared");
      updateState({
        state: "working",
        message: "Running...",
        promptToolType: undefined,
        promptToolDetail: undefined,
        promptDescription: undefined,
      });
    },
  });

  console.log("Server ready. Waiting for hooks and 3DS connections...");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
