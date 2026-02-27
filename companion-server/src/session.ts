import { createSDKAdapter, type SDKAdapter, type SDKAdapterCallbacks } from "./adapters/sdk";

export const MAX_SLOTS = 4;

export interface ManagedSession {
  slot: number;
  sdkSessionId: string | null;
  status: "spawning" | "active" | "idle" | "ending";
  lastActivity: number;
  adapter: SDKAdapter;
}

// Slot -> session
const sessions = new Map<number, ManagedSession>();

// SDK session_id -> slot (for routing)
const sessionIdMap = new Map<string, number>();

// Callbacks wired by the server for broadcasting to 3DS
let adapterCallbacks: SDKAdapterCallbacks | null = null;

export function setAdapterCallbacks(callbacks: SDKAdapterCallbacks) {
  adapterCallbacks = callbacks;
}

export function getSession(slot: number): ManagedSession | undefined {
  return sessions.get(slot);
}

export function getAllSessions(): ManagedSession[] {
  return Array.from(sessions.values());
}

export function getSlotForSessionId(sessionId: string): number | undefined {
  return sessionIdMap.get(sessionId);
}

/**
 * Link an SDK session_id to a slot.
 */
export function linkSession(sdkSessionId: string, slot: number): void {
  const session = sessions.get(slot);
  if (session) {
    session.sdkSessionId = sdkSessionId;
    session.status = "active";
    session.lastActivity = Date.now();
    sessionIdMap.set(sdkSessionId, slot);
    console.log(`[session] Linked SDK session ${sdkSessionId} to slot ${slot}`);
  }
}

/**
 * Spawn a new Claude Code session in the given slot using the Agent SDK.
 */
export async function spawnSession(slot: number, initialPrompt?: string): Promise<boolean> {
  if (slot < 0 || slot >= MAX_SLOTS) {
    console.error(`[session] Invalid slot: ${slot}`);
    return false;
  }

  if (sessions.has(slot)) {
    console.error(`[session] Slot ${slot} already occupied`);
    return false;
  }

  if (!adapterCallbacks) {
    console.error(`[session] No adapter callbacks configured`);
    return false;
  }

  const adapter = createSDKAdapter(slot, adapterCallbacks);

  const session: ManagedSession = {
    slot,
    sdkSessionId: null,
    status: "spawning",
    lastActivity: Date.now(),
    adapter,
  };

  sessions.set(slot, session);
  console.log(`[session] Created SDK session in slot ${slot}`);

  // Start the SDK session
  adapter.start(initialPrompt);

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
  if (session.sdkSessionId) {
    sessionIdMap.delete(session.sdkSessionId);
  }

  // Stop the SDK adapter
  session.adapter.stop();

  sessions.delete(slot);
  console.log(`[session] Killed session in slot ${slot}`);
}

/**
 * Get the adapter for a slot.
 */
export function getAdapterForSlot(slot: number): SDKAdapter | undefined {
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
 * Health check: verify SDK sessions are still alive.
 */
export async function healthCheck(): Promise<void> {
  for (const [slot, session] of sessions) {
    if (session.adapter.status === "done" || session.adapter.status === "error") {
      console.log(`[session] Health check: slot ${slot} is ${session.adapter.status}, cleaning up`);
      if (session.sdkSessionId) {
        sessionIdMap.delete(session.sdkSessionId);
      }
      sessions.delete(slot);
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
