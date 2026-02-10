import { $ } from "bun";

const TMUX_SESSION = "claude-raids";
const POLL_INTERVAL_MS = 300;

export interface PromptInfo {
  toolType: string;
  toolDetail: string;
  description: string;
}

export interface ScraperCallbacks {
  onPromptAppeared(prompt: PromptInfo): void;
  onPromptDisappeared(): void;
}

export async function captureTmuxPane(): Promise<string> {
  try {
    const result = await $`tmux capture-pane -p -t ${TMUX_SESSION} -S -30`.quiet();
    return result.text();
  } catch {
    return "";
  }
}

export function parsePrompt(content: string): PromptInfo | null {
  const lines = content.split("\n");

  // Find the "Do you want to proceed?" line
  let promptIdx = -1;
  for (let i = 0; i < lines.length; i++) {
    if (lines[i].includes("Do you want to proceed?")) {
      promptIdx = i;
      break;
    }
  }

  if (promptIdx < 0) return null;

  // Validate: there should be a "Yes" option after the prompt line
  let hasYesOption = false;
  for (let i = promptIdx + 1; i < lines.length; i++) {
    if (/^\s*(>?\s*)?\d+\.\s*Yes/.test(lines[i]) || /Yes/i.test(lines[i])) {
      hasYesOption = true;
      break;
    }
  }
  if (!hasYesOption) return null;

  // Walk backwards from the prompt line to extract tool info.
  // Claude Code renders something like:
  //
  //   ╭──────────────────────────────────────╮
  //   │ Bash command                         │
  //   │ git -C /Users/.../rAI3DS status      │
  //   │ Show working tree status             │
  //   ╰──────────────────────────────────────╯
  //   Do you want to proceed?
  //
  // We look for lines inside the box (between the border lines).

  let toolType = "";
  let toolDetail = "";
  let description = "";

  // Find the box content above the prompt line
  const boxLines: string[] = [];
  let boxTopIdx = -1;

  const searchLimit = Math.max(0, promptIdx - 15);
  for (let i = promptIdx - 1; i >= searchLimit; i--) {
    const line = lines[i].trim();
    // Box borders use unicode box-drawing characters
    if (line.startsWith("\u256D") || line.startsWith("+") || line.startsWith("\u250C")) {
      boxTopIdx = i;
      break;
    }
    if (line.startsWith("\u2570") || line.startsWith("\u2514")) {
      // Bottom border, skip
      continue;
    }
    if (line === "") continue;

    // Strip box-drawing border characters (│)
    let stripped = line;
    if (stripped.startsWith("\u2502")) stripped = stripped.slice(1);
    if (stripped.endsWith("\u2502")) stripped = stripped.slice(0, -1);
    stripped = stripped.trim();

    if (stripped) {
      boxLines.unshift(stripped);
    }
  }

  // If no box border found, the text is unreliable — use only the
  // 3 lines directly above the prompt (closest to the prompt)
  if (boxTopIdx === -1 && boxLines.length > 3) {
    boxLines.splice(0, boxLines.length - 3);
  }

  if (boxLines.length >= 1) {
    toolType = boxLines[0]; // e.g. "Bash command"
  }
  if (boxLines.length >= 2) {
    toolDetail = boxLines[1]; // e.g. "git -C /Users/.../rAI3DS status"
  }
  if (boxLines.length >= 3) {
    description = boxLines[2]; // e.g. "Show working tree status"
  }

  // If we couldn't extract anything useful, use a generic message
  if (!toolType && !toolDetail) {
    toolType = "Tool";
    toolDetail = "Permission requested";
  }

  return { toolType, toolDetail, description };
}

let intervalId: ReturnType<typeof setInterval> | null = null;
let lastPrompt: PromptInfo | null = null;
let tmuxSessionAlive = false;

export function isTmuxAvailable(): boolean {
  return tmuxSessionAlive;
}

export function startScraper(callbacks: ScraperCallbacks) {
  console.log(`[scraper] Starting tmux scraper (every ${POLL_INTERVAL_MS}ms)`);

  const tick = async () => {
    // Check if tmux session exists
    try {
      await $`tmux has-session -t ${TMUX_SESSION}`.quiet();
      tmuxSessionAlive = true;
    } catch {
      tmuxSessionAlive = false;
      if (lastPrompt) {
        lastPrompt = null;
        callbacks.onPromptDisappeared();
      }
      return;
    }

    const content = await captureTmuxPane();
    if (!content) {
      if (lastPrompt) {
        lastPrompt = null;
        callbacks.onPromptDisappeared();
      }
      return;
    }

    const prompt = parsePrompt(content);

    if (prompt && !lastPrompt) {
      // idle -> prompting
      lastPrompt = prompt;
      callbacks.onPromptAppeared(prompt);
    } else if (prompt && lastPrompt) {
      // Check if prompt changed
      if (
        prompt.toolType !== lastPrompt.toolType ||
        prompt.toolDetail !== lastPrompt.toolDetail
      ) {
        lastPrompt = prompt;
        callbacks.onPromptAppeared(prompt);
      }
      // Same prompt: no-op
    } else if (!prompt && lastPrompt) {
      // prompting -> idle
      lastPrompt = null;
      callbacks.onPromptDisappeared();
    }
  };

  tick();
  intervalId = setInterval(tick, POLL_INTERVAL_MS);
}

export function stopScraper() {
  if (intervalId) {
    clearInterval(intervalId);
    intervalId = null;
    lastPrompt = null;
  }
}
