import type {
  PermissionRequestHook,
  PermissionRequestResponse,
  PreToolHook,
  PostToolHook,
  AgentStatus,
  AgentStatusMessage,
  DSMessage,
  SessionStartHook,
  SessionEndHook,
  StopHook,
  PendingPermission,
} from "./types";
import type { ServerWebSocket } from "bun";

const PORT = 3333;
const HOST = "0.0.0.0";
const MAX_SLOTS = 4;
const PERMISSION_TIMEOUT_MS = 90_000; // 90 seconds before auto-deny

// In-memory state — one per slot
const agentStates: AgentStatus[] = [];
for (let i = 0; i < MAX_SLOTS; i++) {
  agentStates.push({
    name: i === 0 ? "claude" : `agent-${i}`,
    state: "idle",
    progress: -1,
    message: "Waiting for Claude session...",
    lastUpdate: Date.now(),
    contextPercent: 0,
    slot: i,
    active: false,
  });
}

// WebSocket clients (Bun native)
const wsClients = new Set<ServerWebSocket>();

// Auto-edit state (synced with 3DS)
let autoEditEnabled = false;

// Session ID to slot mapping
const sessionSlots = new Map<string, number>();

// Pending permission requests (held HTTP responses)
const pendingPermissions = new Map<number, PendingPermission>();

// Request counter for unique IDs
let requestCounter = 0;

export function getAgentState(slot: number = 0): AgentStatus {
  return agentStates[slot];
}

export function getAgentStates(): AgentStatus[] {
  return agentStates;
}

export function isAutoEditEnabled(): boolean {
  return autoEditEnabled;
}

function broadcast(data: string) {
  for (const client of wsClients) {
    try {
      client.send(data);
    } catch {
      wsClients.delete(client);
    }
  }
}

function broadcastSlotState(slot: number) {
  const state = agentStates[slot];
  const message: AgentStatusMessage = {
    type: "agent_status",
    agent: state.name,
    state: state.state,
    progress: state.progress,
    message: state.message,
    contextPercent: state.contextPercent,
    promptToolType: state.promptToolType,
    promptToolDetail: state.promptToolDetail,
    promptDescription: state.promptDescription,
    autoEdit: autoEditEnabled,
    slot: state.slot,
    active: state.active,
  };
  broadcast(JSON.stringify(message));
}

function broadcastAllSlots() {
  for (let i = 0; i < MAX_SLOTS; i++) {
    broadcastSlotState(i);
  }
}

export function updateState(slot: number, updates: Partial<AgentStatus>) {
  Object.assign(agentStates[slot], updates, { lastUpdate: Date.now() });
  broadcastSlotState(slot);
}

export function updateContextPercent(percent: number, slot: number = 0) {
  if (agentStates[slot].contextPercent === percent) return;
  agentStates[slot].contextPercent = percent;
  broadcastSlotState(slot);
}

export function getClientCount(): number {
  return wsClients.size;
}

function resolveSlot(sessionId?: string): number {
  if (!sessionId) return 0;
  const slot = sessionSlots.get(sessionId);
  if (slot !== undefined) return slot;
  
  // Auto-assign to first available slot
  for (let i = 0; i < MAX_SLOTS; i++) {
    if (!agentStates[i].active) {
      sessionSlots.set(sessionId, i);
      return i;
    }
  }
  // Fallback to slot 0 if all active
  sessionSlots.set(sessionId, 0);
  return 0;
}

function extractToolDetail(toolInput: Record<string, unknown>): string {
  const keys = ["command", "file_path", "pattern", "query", "url"] as const;
  for (const key of keys) {
    if (typeof toolInput[key] === "string") return toolInput[key] as string;
  }
  const firstVal = Object.values(toolInput)[0];
  return typeof firstVal === "string" ? firstVal : "";
}

// Create a permission response
function makePermissionResponse(behavior: "allow" | "deny", message?: string): PermissionRequestResponse {
  return {
    hookSpecificOutput: {
      hookEventName: "PermissionRequest",
      decision: {
        behavior,
        ...(message && { message }),
      },
    },
  };
}

// Resolve a pending permission request
function resolvePendingPermission(slot: number, action: "yes" | "always" | "no") {
  const pending = pendingPermissions.get(slot);
  if (!pending) {
    console.log(`[permission] No pending request for slot ${slot}`);
    return false;
  }

  let behavior: "allow" | "deny";
  let message: string;
  
  switch (action) {
    case "yes":
      behavior = "allow";
      message = "Approved by 3DS user";
      break;
    case "always":
      behavior = "allow";
      message = "Always approved by 3DS user";
      break;
    case "no":
      behavior = "deny";
      message = "Denied by 3DS user";
      break;
  }

  console.log(`[permission] Resolving slot ${slot} with ${behavior}: ${message}`);
  pending.resolve(makePermissionResponse(behavior, message));
  pendingPermissions.delete(slot);

  // Update state to working
  updateState(slot, {
    state: action === "no" ? "idle" : "working",
    message: action === "no" ? "Permission denied" : `Running: ${pending.toolName}`,
    promptToolType: undefined,
    promptToolDetail: undefined,
    promptDescription: undefined,
  });

  return true;
}

// Handle incoming WebSocket messages from 3DS
async function handleWsMessage(msg: DSMessage) {
  console.log("[ws] Received:", JSON.stringify(msg));

  const targetSlot = "slot" in msg ? (msg.slot ?? 0) : 0;

  if (msg.type === "action") {
    // Handle permission actions from 3DS
    const resolved = resolvePendingPermission(targetSlot, msg.action);
    if (!resolved && msg.action === "escape") {
      // Escape without pending permission - just clear state
      updateState(targetSlot, {
        state: "idle",
        message: "Cancelled",
        promptToolType: undefined,
        promptToolDetail: undefined,
        promptDescription: undefined,
      });
    }
  } else if (msg.type === "config") {
    if (msg.autoEdit !== undefined) {
      autoEditEnabled = msg.autoEdit;
      console.log(`[ws] Auto-edit set to: ${autoEditEnabled}`);
      broadcastAllSlots();
    }
  }
  // Note: spawn_request and command types removed - no tmux management
}

export function startServer() {
  const server = Bun.serve({
    hostname: HOST,
    port: PORT,

    async fetch(req, server) {
      // WebSocket upgrade
      if (req.headers.get("upgrade")?.toLowerCase() === "websocket") {
        if (server.upgrade(req)) {
          return undefined;
        }
        return new Response("WebSocket upgrade failed", { status: 400 });
      }

      const url = new URL(req.url);
      const path = url.pathname;

      // Health check
      if (path === "/health" && req.method === "GET") {
        return Response.json({
          status: "ok",
          agents: agentStates,
          autoEdit: autoEditEnabled,
          wsClients: wsClients.size,
          pendingPermissions: pendingPermissions.size,
          sessions: Array.from(sessionSlots.entries()).map(([id, slot]) => ({
            sessionId: id,
            slot,
            active: agentStates[slot].active,
          })),
        });
      }

      // PermissionRequest hook - the primary permission control hook
      // This is held until 3DS responds or timeout
      if (path === "/hook/permission-request" && req.method === "POST") {
        try {
          const body = (await req.json()) as PermissionRequestHook;
          const slot = resolveSlot(body.session_id);
          const toolName = body.tool_name || "Unknown";
          const toolDetail = body.tool_input ? extractToolDetail(body.tool_input) : "";
          const description = typeof body.tool_input?.description === "string"
            ? body.tool_input.description
            : "";

          console.log(`[hook] permission-request (slot ${slot}): ${toolName} — ${toolDetail}`);

          // Check auto-edit for edit/write tools
          const AUTO_EDIT_PATTERNS = ["edit", "write", "notebook"];
          const isEditTool = AUTO_EDIT_PATTERNS.some((p) =>
            toolName.toLowerCase().includes(p)
          );
          if (autoEditEnabled && isEditTool) {
            console.log(`[auto-edit] Auto-approving: ${toolName}`);
            updateState(slot, {
              state: "working",
              progress: -1,
              message: `Auto-approved: ${toolName}`,
              promptToolType: undefined,
              promptToolDetail: undefined,
              promptDescription: undefined,
            });
            return Response.json(makePermissionResponse("allow", "Auto-approved by rAI3DS"));
          }

          // Set state to waiting - 3DS will see this
          updateState(slot, {
            state: "waiting",
            message: `${toolName}: ${toolDetail}`,
            promptToolType: toolName,
            promptToolDetail: toolDetail,
            promptDescription: description,
          });

          // Create a promise that will be resolved when 3DS responds
          const requestId = `perm-${++requestCounter}`;
          
          return new Promise<Response>((resolve) => {
            const pending: PendingPermission = {
              requestId,
              slot,
              toolName,
              toolInput: body.tool_input || {},
              description,
              sessionId: body.session_id,
              createdAt: Date.now(),
              resolve: (response) => {
                resolve(Response.json(response));
              },
            };

            pendingPermissions.set(slot, pending);

            // Set timeout - deny if no response
            setTimeout(() => {
              if (pendingPermissions.has(slot) && pendingPermissions.get(slot)?.requestId === requestId) {
                console.log(`[permission] Timeout for slot ${slot}, denying`);
                pendingPermissions.delete(slot);
                updateState(slot, {
                  state: "idle",
                  message: "Permission timed out",
                  promptToolType: undefined,
                  promptToolDetail: undefined,
                  promptDescription: undefined,
                });
                resolve(Response.json(makePermissionResponse("deny", "Permission request timed out")));
              }
            }, PERMISSION_TIMEOUT_MS);
          });
        } catch (e) {
          console.error("[hook] permission-request error:", e);
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Session start hook - registers session without tmux
      if (path === "/hook/session-start" && req.method === "POST") {
        try {
          const body = (await req.json()) as SessionStartHook;
          if (body.session_id) {
            const slot = resolveSlot(body.session_id);
            console.log(`[hook] session-start (slot ${slot}): ${body.session_id}`);
            agentStates[slot].active = true;
            agentStates[slot].name = slot === 0 ? "claude" : `claude-${slot}`;
            updateState(slot, { state: "idle", message: "Session started" });
          }
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Session end hook
      if (path === "/hook/session-end" && req.method === "POST") {
        try {
          const body = (await req.json()) as SessionEndHook;
          if (body.session_id) {
            const slot = sessionSlots.get(body.session_id) ?? 0;
            console.log(`[hook] session-end (slot ${slot}): ${body.session_id}`);
            
            // Clear any pending permission
            if (pendingPermissions.has(slot)) {
              const pending = pendingPermissions.get(slot)!;
              pending.resolve(makePermissionResponse("deny", "Session ended"));
              pendingPermissions.delete(slot);
            }
            
            sessionSlots.delete(body.session_id);
            agentStates[slot].active = false;
            updateState(slot, { state: "done", message: "Session ended" });
          }
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Pre-tool hook (observability only - does not control permissions)
      if (path === "/hook/pre-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PreToolHook;
          const slot = resolveSlot(body.session_id);
          const toolName = body.tool_name || body.tool || "Unknown";
          console.log(`[hook] pre-tool (slot ${slot}): ${toolName}`);
          // Just acknowledge - no permission control here
          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Post-tool hook
      if (path === "/hook/post-tool" && req.method === "POST") {
        try {
          const body = (await req.json()) as PostToolHook;
          const slot = resolveSlot(body.session_id);
          const toolName = body.tool_name || body.tool || "Unknown";
          console.log(`[hook] post-tool (slot ${slot}): ${toolName}`);

          updateState(slot, {
            state: body.error ? "error" : "working",
            progress: -1,
            message: body.error || `Done: ${toolName}`,
            promptToolType: undefined,
            promptToolDetail: undefined,
            promptDescription: undefined,
          });

          return Response.json({ ok: true });
        } catch (e) {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      // Stop hook
      if (path === "/hook/stop" && req.method === "POST") {
        try {
          const body = (await req.json()) as StopHook;
          const slot = resolveSlot(body.session_id);
          console.log(`[hook] stop (slot ${slot})`);
          updateState(slot, { state: "idle", message: "Ready" });
          return Response.json({ ok: true });
        } catch {
          return Response.json({ error: "Invalid JSON" }, { status: 400 });
        }
      }

      return Response.json({ error: "Not found" }, { status: 404 });
    },

    websocket: {
      open(ws) {
        console.log("[ws] 3DS client connected");
        wsClients.add(ws);
        // Send current state of all slots to the new client
        broadcastAllSlots();
      },

      message(ws, data) {
        try {
          const text =
            typeof data === "string" ? data : new TextDecoder().decode(data);
          const msg = JSON.parse(text) as DSMessage;
          handleWsMessage(msg);
        } catch (e) {
          console.error("[ws] Invalid message:", e);
        }
      },

      close(ws) {
        console.log("[ws] 3DS client disconnected");
        wsClients.delete(ws);
      },
    },
  });

  console.log(
    `Server listening on http://${HOST}:${PORT} (HTTP hooks + WebSocket)`
  );
  return server;
}
