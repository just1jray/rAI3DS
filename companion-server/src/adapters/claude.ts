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
  sendApproval(): Promise<void>;
  sendDenial(): Promise<void>;
  sendInput(text: string): Promise<void>;
}

export function createClaudeAdapter(tmuxSession: string = DEFAULT_TMUX_SESSION): ClaudeAdapter {
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
      const running = await this.isRunning();
      if (running) {
        console.log(`[claude] Session ${tmuxSession} already running`);
        return;
      }

      console.log(`[claude] Starting tmux session: ${tmuxSession}`);
      await $`tmux new-session -d -s ${tmuxSession} ${command}`;
    },

    async stop() {
      const running = await this.isRunning();
      if (!running) {
        console.log(`[claude] Session ${tmuxSession} not running`);
        return;
      }

      console.log(`[claude] Stopping tmux session: ${tmuxSession}`);
      await $`tmux kill-session -t ${tmuxSession}`;
    },

    async sendYes() {
      const running = await this.isRunning();
      if (!running) {
        console.error(`[claude] Cannot send Yes: session ${tmuxSession} not running`);
        return;
      }
      console.log(`[claude] Sending Yes to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Enter`;
    },

    async sendAlways() {
      const running = await this.isRunning();
      if (!running) {
        console.error(`[claude] Cannot send Always: session ${tmuxSession} not running`);
        return;
      }
      console.log(`[claude] Sending Always to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Down Enter`;
    },

    async sendNo() {
      const running = await this.isRunning();
      if (!running) {
        console.error(`[claude] Cannot send No: session ${tmuxSession} not running`);
        return;
      }
      console.log(`[claude] Sending No to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Down Down Enter`;
    },

    async sendEscape() {
      const running = await this.isRunning();
      if (!running) {
        console.error(`[claude] Cannot send Escape: session ${tmuxSession} not running`);
        return;
      }
      console.log(`[claude] Sending Escape to ${tmuxSession}`);
      await $`tmux send-keys -t ${tmuxSession} Escape`;
    },

    // Backward-compat aliases
    async sendApproval() {
      return this.sendYes();
    },

    async sendDenial() {
      return this.sendNo();
    },

    async sendInput(text: string) {
      const running = await this.isRunning();
      if (!running) {
        console.error(`[claude] Cannot send input: session ${tmuxSession} not running`);
        return;
      }

      console.log(`[claude] Sending input to ${tmuxSession}: ${text}`);
      await $`tmux send-keys -t ${tmuxSession} ${text} Enter`;
    },
  };
}
