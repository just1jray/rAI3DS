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
    SessionStart?: HookEntry[];
    SessionEnd?: HookEntry[];
    Stop?: HookEntry[];
    UserPromptSubmit?: HookEntry[];
  };
  [key: string]: unknown;
}

const RAIDS_MARKER = "localhost:3333";

function makeHookCommand(endpoint: string): string {
  return `curl -s --connect-timeout 1 --max-time 2 -X POST http://localhost:3333/hook/${endpoint} -H "Content-Type: application/json" -d @-; exit 0`;
}

const RAIDS_HOOKS: Record<string, HookEntry[]> = {
  PreToolUse: [
    {
      matcher: "",
      hooks: [{ type: "command" as const, command: makeHookCommand("pre-tool") }],
    },
  ],
  PostToolUse: [
    {
      matcher: "",
      hooks: [{ type: "command" as const, command: makeHookCommand("post-tool") }],
    },
  ],
  Stop: [
    {
      matcher: "",
      hooks: [{ type: "command" as const, command: makeHookCommand("stop") }],
    },
  ],
  UserPromptSubmit: [
    {
      matcher: "",
      hooks: [{ type: "command" as const, command: makeHookCommand("user-prompt") }],
    },
  ],
};

function isRaidsHook(entry: HookEntry): boolean {
  return entry.hooks?.some((cmd) => cmd.command.includes(RAIDS_MARKER)) ?? false;
}

export async function installHooks(): Promise<boolean> {
  console.log("[hooks] Installing rAI3DS hooks to Claude Code...");

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
