import { startServer, setClaudeAdapter, updateState, isAutoEditEnabled } from "./server";
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

  // Auto-edit: tool type patterns that match edit/write operations
  const AUTO_EDIT_PATTERNS = ["edit", "write", "notebook"];

  // Start tmux screen scraper to detect permission prompts
  startScraper({
    onPromptAppeared(prompt) {
      console.log(
        `[scraper] Prompt appeared: ${prompt.toolType} — ${prompt.toolDetail}`
      );

      // Auto-edit: send YES keystroke automatically for edit tools
      const isEditTool = AUTO_EDIT_PATTERNS.some((p) =>
        prompt.toolType.toLowerCase().includes(p)
      );
      if (isAutoEditEnabled() && isEditTool) {
        console.log(`[auto-edit] Auto-approving: ${prompt.toolType}`);
        claudeAdapter.sendYes().catch((e: unknown) =>
          console.error("[auto-edit] keystroke error:", e)
        );
        updateState({
          state: "working",
          progress: -1,
          message: `Auto-approved: ${prompt.toolType}`,
          promptToolType: undefined,
          promptToolDetail: undefined,
          promptDescription: undefined,
        });
        return;
      }

      // Normal flow: show prompt on 3DS for user to approve/deny
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
