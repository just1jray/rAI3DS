import type { Subprocess } from "bun";
import { cancelPendingPermissions, sendInterrupt } from "./cli-handler";
import { updateState } from "./server";

const MAX_SLOTS = 4;

export interface ManagedSession {
  slot: number;
  cliSessionId: string | null;
  process: Subprocess;
  pid: number;
  status: "spawning" | "active" | "idle" | "ending";
  lastActivity: number;
  resumeSessionId?: string;
}

const sessions = new Map<number, ManagedSession>();

export function getSession(slot: number): ManagedSession | undefined {
  return sessions.get(slot);
}

export function getAllSessions(): ManagedSession[] {
  return Array.from(sessions.values());
}

export function spawnSession(slot: number, cwd?: string, resumeId?: string): boolean {
  if (slot < 0 || slot >= MAX_SLOTS) {
    console.error(`[session] Invalid slot: ${slot}`);
    return false;
  }

  if (sessions.has(slot)) {
    console.error(`[session] Slot ${slot} already occupied`);
    return false;
  }

  const args = [
    "claude",
    "--sdk-url", `ws://localhost:3333/ws/cli/${slot}`,
    "--print",
    "--output-format", "stream-json",
    "--input-format", "stream-json",
    "--verbose",
    "-p", "",
  ];
  if (resumeId) {
    args.push("--resume", resumeId);
  }

  try {
    const proc = Bun.spawn(args, {
      cwd: cwd ?? process.cwd(),
      stdout: "ignore",
      stderr: "pipe",
    });

    const session: ManagedSession = {
      slot,
      cliSessionId: null,
      process: proc,
      pid: proc.pid,
      status: "spawning",
      lastActivity: Date.now(),
    };
    sessions.set(slot, session);

    console.log(`[session] Spawned slot ${slot} (PID ${proc.pid})`);

    // Monitor exit
    proc.exited.then((exitCode) => {
      console.log(`[session] slot ${slot} (PID ${proc.pid}) exited with code ${exitCode}`);
      sessions.delete(slot);
      cancelPendingPermissions(slot);
      updateState(slot, { state: "done", active: false });
    });

    return true;
  } catch (e) {
    console.error(`[session] Failed to spawn slot ${slot}:`, e);
    return false;
  }
}

export function killSession(slot: number): void {
  const session = sessions.get(slot);
  if (!session) return;

  sendInterrupt(slot);
  session.process.kill();
  // proc.exited.then() handles cleanup
}

export function healthCheck(): void {
  for (const [slot, session] of sessions) {
    if (session.process.exitCode !== null) {
      // Dead process not yet cleaned up by exited handler (safety net)
      console.log(`[session] Health check: slot ${slot} (PID ${session.pid}) is dead, cleaning up`);
      sessions.delete(slot);
      cancelPendingPermissions(slot);
      updateState(slot, { state: "done", active: false });
    }
  }
}

export function autoSpawnDefaultSession(): void {
  setTimeout(() => {
    console.log("[session] Auto-spawning default session (slot 0)...");
    const success = spawnSession(0);
    if (!success) {
      console.error("[session] Failed to auto-spawn slot 0");
    }
  }, 100);
}

export function findFreeSlot(): number | null {
  for (let i = 0; i < MAX_SLOTS; i++) {
    if (!sessions.has(i)) return i;
  }
  return null;
}
