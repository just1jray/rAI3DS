import { $ } from "bun";

const TMUX_SESSION = "claude-raids";

export interface ClaudeAdapter {
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

export function createClaudeAdapter(): ClaudeAdapter {
  return {
    async isRunning() {
      try {
        await $`tmux has-session -t ${TMUX_SESSION}`.quiet();
        return true;
      } catch {
        return false;
      }
    },

    async start(command = "claude") {
      const running = await this.isRunning();
      if (running) {
        console.log(`[claude] Session ${TMUX_SESSION} already running`);
        return;
      }

      console.log(`[claude] Starting tmux session: ${TMUX_SESSION}`);
      await $`tmux new-session -d -s ${TMUX_SESSION} ${command}`;
    },

    async stop() {
      const running = await this.isRunning();
      if (!running) {
        console.log(`[claude] Session ${TMUX_SESSION} not running`);
        return;
      }

      console.log(`[claude] Stopping tmux session: ${TMUX_SESSION}`);
      await $`tmux kill-session -t ${TMUX_SESSION}`;
    },

    async sendYes() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot send Yes: session not running");
        return;
      }
      console.log("[claude] Sending Yes (Enter — option 1 is pre-selected)");
      await $`tmux send-keys -t ${TMUX_SESSION} Enter`;
    },

    async sendAlways() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot send Always: session not running");
        return;
      }
      console.log("[claude] Sending Always (Down + Enter)");
      await $`tmux send-keys -t ${TMUX_SESSION} Down Enter`;
    },

    async sendNo() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot send No: session not running");
        return;
      }
      console.log("[claude] Sending No (Down Down Enter)");
      await $`tmux send-keys -t ${TMUX_SESSION} Down Down Enter`;
    },

    async sendEscape() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot send Escape: session not running");
        return;
      }
      console.log("[claude] Sending Escape");
      await $`tmux send-keys -t ${TMUX_SESSION} Escape`;
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
        console.error("[claude] Cannot send input: session not running");
        return;
      }

      console.log(`[claude] Sending input: ${text}`);
      await $`tmux send-keys -t ${TMUX_SESSION} ${text} Enter`;
    },
  };
}
