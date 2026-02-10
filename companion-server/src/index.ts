import { startServer, updateState, isAutoEditEnabled, getPendingToolData } from "./server";
import { installHooks, uninstallHooks } from "./hooks";
import { startContextTracker } from "./context";
import { startScraper } from "./scraper";
import { initDefaultSession, getAdapterForSlot, healthCheck } from "./session";

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

  // Initialize default session (slot 0 — existing claude-raids tmux)
  const defaultSession = initDefaultSession();
  console.log(`[session] Default session initialized: ${defaultSession.tmuxPaneId}`);

  startServer();
  startContextTracker(10_000);

  // Auto-edit: tool type patterns that match edit/write operations
  const AUTO_EDIT_PATTERNS = ["edit", "write", "notebook"];

  // Start tmux screen scraper for slot 0 (default session)
  startScraper({
    onPromptAppeared(prompt) {
      const slot = 0; // Scraper always targets slot 0
      const hookData = getPendingToolData(slot);
      const toolType = hookData?.toolType || prompt.toolType;
      const toolDetail = hookData?.toolDetail || prompt.toolDetail;
      const description = hookData?.description || prompt.description;

      console.log(
        `[scraper] Prompt appeared (slot ${slot}): ${toolType} — ${toolDetail}${hookData ? " (hook)" : " (scraped)"}`
      );

      const isEditTool = AUTO_EDIT_PATTERNS.some((p) =>
        toolType.toLowerCase().includes(p)
      );
      if (isAutoEditEnabled() && isEditTool) {
        console.log(`[auto-edit] Auto-approving: ${toolType}`);
        const adapter = getAdapterForSlot(slot);
        adapter?.sendYes().catch((e: unknown) =>
          console.error("[auto-edit] keystroke error:", e)
        );
        updateState(slot, {
          state: "working",
          progress: -1,
          message: `Auto-approved: ${toolType}`,
          promptToolType: undefined,
          promptToolDetail: undefined,
          promptDescription: undefined,
        });
        return;
      }

      updateState(slot, {
        state: "waiting",
        message: `${toolType}: ${toolDetail}`,
        promptToolType: toolType,
        promptToolDetail: toolDetail,
        promptDescription: description,
      });
    },
    onPromptDisappeared() {
      console.log("[scraper] Prompt disappeared (slot 0)");
      updateState(0, {
        state: "working",
        message: "Running...",
        promptToolType: undefined,
        promptToolDetail: undefined,
        promptDescription: undefined,
      });
    },
  });

  // Health check: every 30s, check for dead tmux sessions
  setInterval(() => {
    healthCheck().catch((e) => console.error("[health] Error:", e));
  }, 30_000);

  console.log("Server ready. Waiting for hooks and 3DS connections...");
}

main().catch((e) => {
  console.error("Fatal error:", e);
  process.exit(1);
});
