import { $ } from "bun";
import { existsSync } from "fs";
import { homedir } from "os";
import { join } from "path";

const CLAUDE_SETTINGS_PATH = join(homedir(), ".claude", "settings.json");
const SERVER_URL = "http://localhost:3333";

interface HookEntry {
  matcher: string;
  hooks: Array<{ type: "command" | "http"; command?: string; url?: string; timeout?: number }>;
}

interface ClaudeSettings {
  hooks?: {
    PermissionRequest?: HookEntry[];
    PreToolUse?: HookEntry[];
    PostToolUse?: HookEntry[];
    SessionStart?: HookEntry[];
    SessionEnd?: HookEntry[];
    Stop?: HookEntry[];
    UserPromptSubmit?: HookEntry[];
  };
  [key: string]: unknown;
}

const RAIDS_MARKER = "localhost:3333";

// HTTP hooks for Claude Code (Aug 2026)
// PermissionRequest is the primary event for permission decisions
// It fires when a tool call needs a permission decision and can hold the request
const RAIDS_HOOKS: Record<string, HookEntry[]> = {
  // Primary hook: PermissionRequest fires when permission dialog would appear
  // The HTTP response controls allow/deny via hookSpecificOutput.decision.behavior
  PermissionRequest: [
    {
      matcher: "", // Match all tools
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/permission-request`,
          timeout: 120, // 2 minutes to allow 3DS user to respond
        },
      ],
    },
  ],
  // SessionStart for session registration (no tmux required)
  SessionStart: [
    {
      matcher: "",
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/session-start`,
          timeout: 5,
        },
      ],
    },
  ],
  // SessionEnd for cleanup
  SessionEnd: [
    {
      matcher: "",
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/session-end`,
          timeout: 5,
        },
      ],
    },
  ],
  // PreToolUse for observability (does not control permissions since Aug 2026)
  PreToolUse: [
    {
      matcher: "",
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/pre-tool`,
          timeout: 5,
        },
      ],
    },
  ],
  // PostToolUse for observability
  PostToolUse: [
    {
      matcher: "",
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/post-tool`,
          timeout: 5,
        },
      ],
    },
  ],
  // Stop hook
  Stop: [
    {
      matcher: "",
      hooks: [
        {
          type: "http",
          url: `${SERVER_URL}/hook/stop`,
          timeout: 5,
        },
      ],
    },
  ],
};

function isRaidsHook(entry: HookEntry): boolean {
  return entry.hooks?.some((h) => {
    if (h.type === "http" && h.url?.includes(RAIDS_MARKER)) return true;
    if (h.type === "command" && h.command?.includes(RAIDS_MARKER)) return true;
    return false;
  }) ?? false;
}

export async function installHooks(): Promise<boolean> {
  console.log("[hooks] Installing rAI3DS HTTP hooks to Claude Code...");

  let settings: ClaudeSettings = {};

  if (existsSync(CLAUDE_SETTINGS_PATH)) {
    try {
      const content = await Bun.file(CLAUDE_SETTINGS_PATH).text();
      settings = JSON.parse(content);
      console.log("[hooks] Found existing Claude settings");
    } catch (e) {
      console.error("[hooks] Failed to parse existing settings:", e);
      return false;
    }
  } else {
    console.log("[hooks] Creating new Claude settings file");
    const claudeDir = join(homedir(), ".claude");
    await $`mkdir -p ${claudeDir}`;
  }

  settings.hooks = settings.hooks || {};

  // Install each hook type
  for (const [eventType, hookEntries] of Object.entries(RAIDS_HOOKS)) {
    const key = eventType as keyof typeof settings.hooks;
    const existing = (settings.hooks[key] as HookEntry[] | undefined) || [];
    settings.hooks[key] = [
      ...existing.filter((h) => !isRaidsHook(h)),
      ...hookEntries,
    ] as any;
  }

  try {
    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks installed successfully");
    console.log(`[hooks] Settings written to: ${CLAUDE_SETTINGS_PATH}`);
    console.log("[hooks] Registered events: " + Object.keys(RAIDS_HOOKS).join(", "));
    console.log("");
    console.log("[hooks] Key change: PermissionRequest HTTP hook now controls permissions.");
    console.log("[hooks] The 3DS receives STATE_WAITING and can approve/deny via WebSocket.");
    console.log("[hooks] No tmux session required - sessions register via SessionStart hook.");
    return true;
  } catch (e) {
    console.error("[hooks] Failed to write settings:", e);
    return false;
  }
}

export async function uninstallHooks(): Promise<boolean> {
  console.log("[hooks] Removing rAI3DS hooks from Claude Code...");

  if (!existsSync(CLAUDE_SETTINGS_PATH)) {
    console.log("[hooks] No Claude settings file found");
    return true;
  }

  try {
    const content = await Bun.file(CLAUDE_SETTINGS_PATH).text();
    const settings: ClaudeSettings = JSON.parse(content);

    if (settings.hooks) {
      for (const eventType of Object.keys(RAIDS_HOOKS)) {
        const key = eventType as keyof typeof settings.hooks;
        if (settings.hooks[key]) {
          settings.hooks[key] = (settings.hooks[key] as HookEntry[]).filter(
            (h) => !isRaidsHook(h)
          ) as any;
        }
      }
    }

    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks removed successfully");
    return true;
  } catch (e) {
    console.error("[hooks] Failed to remove hooks:", e);
    return false;
  }
}
