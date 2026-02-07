import { $ } from "bun";
import { existsSync } from "fs";
import { homedir } from "os";
import { join } from "path";

const CLAUDE_SETTINGS_PATH = join(homedir(), ".claude", "settings.json");

interface HookEntry {
  matcher: string;
  hooks: Array<{ type: "command"; command: string }>;
}

interface ClaudeSettings {
  hooks?: {
    PreToolUse?: HookEntry[];
    PostToolUse?: HookEntry[];
  };
  [key: string]: unknown;
}

const RAIDS_HOOKS = {
  PreToolUse: [
    {
      matcher: "",
      hooks: [
        {
          type: "command" as const,
          command: `curl -s -X POST http://localhost:3333/hook/pre-tool -H 'Content-Type: application/json' -d '{"tool":"$CLAUDE_TOOL_NAME"}'`,
        },
      ],
    },
  ],
  PostToolUse: [
    {
      matcher: "",
      hooks: [
        {
          type: "command" as const,
          command: `curl -s -X POST http://localhost:3333/hook/post-tool -H 'Content-Type: application/json' -d '{"tool":"$CLAUDE_TOOL_NAME"}'`,
        },
      ],
    },
  ],
};

export async function installHooks(): Promise<boolean> {
  console.log("[hooks] Installing rAI3DS hooks to Claude Code...");

  // Read existing settings or create new
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
    // Ensure .claude directory exists
    const claudeDir = join(homedir(), ".claude");
    await $`mkdir -p ${claudeDir}`;
  }

  // Merge hooks
  settings.hooks = settings.hooks || {};
  settings.hooks.PreToolUse = [
    ...(settings.hooks.PreToolUse || []).filter(
      (h) => !h.hooks?.some((cmd) => cmd.command.includes("localhost:3333"))
    ),
    ...RAIDS_HOOKS.PreToolUse,
  ];
  settings.hooks.PostToolUse = [
    ...(settings.hooks.PostToolUse || []).filter(
      (h) => !h.hooks?.some((cmd) => cmd.command.includes("localhost:3333"))
    ),
    ...RAIDS_HOOKS.PostToolUse,
  ];

  // Write settings
  try {
    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks installed successfully");
    console.log(`[hooks] Settings written to: ${CLAUDE_SETTINGS_PATH}`);
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
      settings.hooks.PreToolUse = (settings.hooks.PreToolUse || []).filter(
        (h) => !h.hooks?.some((cmd) => cmd.command.includes("localhost:3333"))
      );
      settings.hooks.PostToolUse = (settings.hooks.PostToolUse || []).filter(
        (h) => !h.hooks?.some((cmd) => cmd.command.includes("localhost:3333"))
      );
    }

    await Bun.write(CLAUDE_SETTINGS_PATH, JSON.stringify(settings, null, 2));
    console.log("[hooks] Hooks removed successfully");
    return true;
  } catch (e) {
    console.error("[hooks] Failed to remove hooks:", e);
    return false;
  }
}
