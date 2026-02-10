import { $ } from "bun";
import { createClaudeAdapter, type ClaudeAdapter } from "./adapters/claude";

const MAX_SLOTS = 4;

export interface ManagedSession {
  slot: number;
  claudeSessionId: string | null;  // set lazily on first hook event
  tmuxPaneId: string;              // tmux pane identifier (e.g. "raids-0")
  status: "spawning" | "active" | "idle" | "ending";
  lastActivity: number;
  adapter: ClaudeAdapter;
}

// Slot → session
const sessions = new Map<number, ManagedSession>();

// Claude session_id → slot (for hook routing)
const sessionIdMap = new Map<string, number>();

export function getSession(slot: number): ManagedSession | undefined {
  return sessions.get(slot);
}

export function getAllSessions(): ManagedSession[] {
  return Array.from(sessions.values());
}

export function getSlotForSessionId(sessionId: string): number | undefined {
  return sessionIdMap.get(sessionId);
}

// Find a slot in 'spawning' status that hasn't been linked yet
function findSpawningSlot(): number | undefined {
  for (const [slot, session] of sessions) {
    if (session.status === "spawning" && !session.claudeSessionId) {
      return slot;
    }
  }
  return undefined;
}

/**
 * Link a Claude session_id to a slot.
 * Called on first hook event from a new session.
 */
export function linkSession(claudeSessionId: string, slot?: number): number | undefined {
  // If already linked, return existing slot
  const existing = sessionIdMap.get(claudeSessionId);
  if (existing !== undefined) return existing;

  // If slot specified, link directly
  if (slot !== undefined && sessions.has(slot)) {
    const session = sessions.get(slot)!;
    session.claudeSessionId = claudeSessionId;
    session.status = "active";
    session.lastActivity = Date.now();
    sessionIdMap.set(claudeSessionId, slot);
    console.log(`[session] Linked session ${claudeSessionId} to slot ${slot}`);
    return slot;
  }

  // Auto-link to first spawning slot (lazy linking from vibecraft pattern)
  const spawningSlot = findSpawningSlot();
  if (spawningSlot !== undefined) {
    const session = sessions.get(spawningSlot)!;
    session.claudeSessionId = claudeSessionId;
    session.status = "active";
    session.lastActivity = Date.now();
    sessionIdMap.set(claudeSessionId, spawningSlot);
    console.log(`[session] Auto-linked session ${claudeSessionId} to spawning slot ${spawningSlot}`);
    return spawningSlot;
  }

  return undefined;
}

/**
 * Route a session_id to its slot, auto-linking if needed.
 * Returns the slot number or undefined if unroutable.
 */
export function resolveSlot(sessionId?: string): number {
  if (!sessionId) return 0; // Default to slot 0 for hooks without session_id

  const slot = sessionIdMap.get(sessionId);
  if (slot !== undefined) return slot;

  // Try auto-linking
  const linked = linkSession(sessionId);
  if (linked !== undefined) return linked;

  // If only one session exists, route there
  if (sessions.size === 1) {
    const onlySlot = sessions.keys().next().value!;
    linkSession(sessionId, onlySlot);
    return onlySlot;
  }

  return 0; // Fallback to slot 0
}

/**
 * Spawn a new Claude session in the given slot.
 */
export async function spawnSession(slot: number): Promise<boolean> {
  if (slot < 0 || slot >= MAX_SLOTS) {
    console.error(`[session] Invalid slot: ${slot}`);
    return false;
  }

  if (sessions.has(slot)) {
    console.error(`[session] Slot ${slot} already occupied`);
    return false;
  }

  const tmuxName = `raids-${slot}`;

  try {
    // Create a new tmux session for this agent
    await $`tmux new-session -d -s ${tmuxName} claude`.quiet();
    console.log(`[session] Spawned tmux session: ${tmuxName}`);
  } catch (e) {
    console.error(`[session] Failed to spawn tmux session:`, e);
    return false;
  }

  const adapter = createClaudeAdapter(tmuxName);

  const session: ManagedSession = {
    slot,
    claudeSessionId: null,
    tmuxPaneId: tmuxName,
    status: "spawning",
    lastActivity: Date.now(),
    adapter,
  };

  sessions.set(slot, session);
  console.log(`[session] Created session in slot ${slot} (tmux: ${tmuxName})`);
  return true;
}

/**
 * Kill the session in the given slot.
 */
export async function killSession(slot: number): Promise<void> {
  const session = sessions.get(slot);
  if (!session) return;

  session.status = "ending";

  // Clean up maps
  if (session.claudeSessionId) {
    sessionIdMap.delete(session.claudeSessionId);
  }

  // Kill tmux pane
  try {
    await $`tmux kill-session -t ${session.tmuxPaneId}`.quiet();
    console.log(`[session] Killed tmux session: ${session.tmuxPaneId}`);
  } catch {
    // Already dead, that's fine
  }

  sessions.delete(slot);
}

/**
 * Initialize slot 0 with the default claude-raids session.
 */
export function initDefaultSession(): ManagedSession {
  const tmuxName = "claude-raids";
  const adapter = createClaudeAdapter(tmuxName);

  const session: ManagedSession = {
    slot: 0,
    claudeSessionId: null,
    tmuxPaneId: tmuxName,
    status: "active",
    lastActivity: Date.now(),
    adapter,
  };

  sessions.set(0, session);
  return session;
}

/**
 * Get the adapter for a slot.
 */
export function getAdapterForSlot(slot: number): ClaudeAdapter | undefined {
  return sessions.get(slot)?.adapter;
}

/**
 * Find the next available slot (for spawn requests).
 */
export function findFreeSlot(): number | undefined {
  for (let i = 0; i < MAX_SLOTS; i++) {
    if (!sessions.has(i)) return i;
  }
  return undefined;
}

/**
 * Health check: poll tmux for dead sessions and clean up.
 */
export async function healthCheck(): Promise<void> {
  for (const [slot, session] of sessions) {
    try {
      await $`tmux has-session -t ${session.tmuxPaneId}`.quiet();
      // Session alive
    } catch {
      // Session dead
      console.log(`[session] Health check: slot ${slot} (${session.tmuxPaneId}) is dead, cleaning up`);
      if (session.claudeSessionId) {
        sessionIdMap.delete(session.claudeSessionId);
      }
      sessions.delete(slot);
    }
  }
}

/**
 * Working timeout: reset stuck 'working' sessions after 2 minutes.
 */
export function checkWorkingTimeouts(): void {
  const TIMEOUT_MS = 2 * 60 * 1000; // 2 minutes
  const now = Date.now();

  for (const [_slot, session] of sessions) {
    if (session.status === "active" && now - session.lastActivity > TIMEOUT_MS) {
      // Will be handled by the caller to reset agent state
    }
  }
}

/**
 * Update last activity timestamp for a slot.
 */
export function touchSession(slot: number): void {
  const session = sessions.get(slot);
  if (session) {
    session.lastActivity = Date.now();
  }
}
