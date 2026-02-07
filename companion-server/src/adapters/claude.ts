import { $ } from "bun";

const TMUX_SESSION = "claude-raids";

export interface ClaudeAdapter {
  isRunning(): Promise<boolean>;
  start(command?: string): Promise<void>;
  stop(): Promise<void>;
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

    async sendApproval() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot approve: session not running");
        return;
      }

      console.log("[claude] Sending approval (y)");
      await $`tmux send-keys -t ${TMUX_SESSION} y Enter`;
    },

    async sendDenial() {
      const running = await this.isRunning();
      if (!running) {
        console.error("[claude] Cannot deny: session not running");
        return;
      }

      console.log("[claude] Sending denial (n)");
      await $`tmux send-keys -t ${TMUX_SESSION} n Enter`;
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
