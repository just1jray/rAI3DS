import { $ } from "bun";

const DEFAULT_TMUX_SESSION = "claude-raids";

export interface ClaudeAdapter {
  tmuxSession: string;
  isRunning(): Promise<boolean>;
  start(command?: string): Promise<void>;
  stop(): Promise<void>;
  sendYes(): Promise<void>;
  sendAlways(): Promise<void>;
  sendNo(): Promise<void>;
  sendEscape(): Promise<void>;
  sendInput(text: string): Promise<void>;
}

export function createClaudeAdapter(tmuxSession: string = DEFAULT_TMUX_SESSION): ClaudeAdapter {
  async function requireRunning(action: string): Promise<boolean> {
    try {
      await $`tmux has-session -t ${tmuxSession}`.quiet();
      return true;
    } catch {
      console.error(`[claude] Cannot send ${action}: session ${tmuxSession} not running`);
      return false;
    }
  }

  return {
    tmuxSession,

    async isRunning() {
      try {
        await $`tmux has-session -t ${tmuxSession}`.quiet();
        return true;
      } catch {
        return false;
      }
    },

    async start(command = "claude") {
      if (await this.isRunning()) {
        console.log(`[claude] Session ${tmuxSession} already running`);
        return;
      }
      console.log(`[claude] Starting tmux session: ${tmuxSession}`);
      await $`tmux new-session -d -s ${tmuxSession} ${command}`;
    },

    async stop() {
      if (!await this.isRunning()) {
        console.log(`[claude] Session ${tmuxSession} not running`);
        return;
      }
      console.log(`[claude] Stopping tmux session: ${tmuxSession}`);
      await $`tmux kill-session -t ${tmuxSession}`;
    },

    async sendYes() {
      if (!await requireRunning("Yes")) return;
      console.log(`[claude] Sending Yes to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Enter`;
    },

    async sendAlways() {
      if (!await requireRunning("Always")) return;
      console.log(`[claude] Sending Always to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Down Enter`;
    },

    async sendNo() {
      if (!await requireRunning("No")) return;
      console.log(`[claude] Sending No to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Down Down Enter`;
    },

    async sendEscape() {
      if (!await requireRunning("Escape")) return;
      console.log(`[claude] Sending Escape to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Escape`;
    },

    async sendInput(text: string) {
      if (!await requireRunning("input")) return;
      console.log(`[claude] Sending input to ${tmuxSession}: ${text}`);
      await $`tmux send-keys -t ${tmuxSession} ${text} Enter`;
    },
  };
}
