import type { ServerWebSocket } from "bun";
import type {
  CLIConnection,
  PendingPermission,
  SDKPermissionUpdate,
  SDKControlResponseAllow,
  SDKControlResponseDeny,
  SDKInterruptRequest,
  SDKUserMessage,
  WSData,
} from "./types";
import { updateState, isAutoEditEnabled } from "./server";

// Auto-edit: tool type patterns that match edit/write operations
const AUTO_EDIT_PATTERNS = ["edit", "write", "notebook"];

// --- State ---

const cliConnections = new Map<number, CLIConnection>();
const pendingPermissions = new Map<string, PendingPermission>();

// --- CLI WebSocket lifecycle ---

export function handleCLIOpen(ws: ServerWebSocket<WSData>, slot: number) {
  cliConnections.set(slot, {
    ws,
    slot,
    sessionId: null,
    model: null,
    contextWindow: 200_000,
  });
  console.log(`[cli] slot ${slot} WebSocket connected`);
}

export function handleCLIMessage(ws: ServerWebSocket<WSData>, data: string) {
  console.log(`[cli:raw] ${data.slice(0, 200)}`);
  const lines = data.split("\n").filter((l: string) => l.trim());
  for (const line of lines) {
    try {
      const msg = JSON.parse(line);
      dispatchCLIMessage(ws, msg);
    } catch (e) {
      console.error("[cli] bad NDJSON line:", line.slice(0, 100));
    }
  }
}

export function handleCLIClose(ws: ServerWebSocket<WSData>, slot: number) {
  cliConnections.delete(slot);
  // Remove pending permissions for this slot
  for (const [id, perm] of pendingPermissions) {
    if (perm.slot === slot) pendingPermissions.delete(id);
  }
  console.log(`[cli] slot ${slot} WebSocket disconnected`);
}

// --- Message dispatch ---

function dispatchCLIMessage(ws: ServerWebSocket<WSData>, msg: any) {
  const slot = findSlotForWs(ws);
  if (slot === undefined) {
    console.error("[cli] message from unknown WebSocket");
    return;
  }

  const conn = cliConnections.get(slot)!;

  switch (msg.type) {
    case "system": {
      if (msg.subtype === "init") {
        conn.sessionId = msg.session_id;
        conn.model = msg.model;
        console.log(`[cli] system/init: slot=${slot} session_id=${msg.session_id} model=${msg.model}`);
        updateState(slot, { state: "idle", active: true, name: msg.model || `claude-${slot}` });
      } else if (msg.subtype === "status") {
        if (msg.status === "compacting") {
          updateState(slot, { state: "working", message: "Compacting context..." });
        } else {
          // status: null means compacting ended
          updateState(slot, { state: "working", message: "" });
        }
      }
      break;
    }

    case "assistant": {
      // Check for API-level errors first
      if (msg.error) {
        console.error(`[cli] assistant error (slot ${slot}): ${msg.error}`);
        updateState(slot, { state: "error", message: msg.error });
        return;
      }

      // Extract text summary from first text content block
      let summary = "";
      if (msg.message?.content) {
        for (const block of msg.message.content) {
          if (block.type === "text" && block.text) {
            summary = block.text.slice(0, 100);
            break;
          }
        }
      }

      // Compute context percent from usage
      let contextPercent = 0;
      if (msg.message?.usage) {
        const usage = msg.message.usage;
        const totalTokens = usage.input_tokens +
          usage.cache_creation_input_tokens +
          usage.cache_read_input_tokens;
        contextPercent = Math.min(100, Math.round((totalTokens / conn.contextWindow) * 100));
      }

      updateState(slot, {
        state: "working",
        contextPercent,
        message: summary,
      });
      break;
    }

    case "control_request": {
      if (msg.request?.subtype === "can_use_tool") {
        const toolName = msg.request.tool_name;
        const toolInput = msg.request.input || {};
        const toolUseId = msg.request.tool_use_id;
        const description = msg.request.description || "";
        const permissionSuggestions = msg.request.permission_suggestions;
        const toolDetail = extractToolDetail(toolInput);

        console.log(`[cli] control_request (slot ${slot}): ${toolName} — ${toolDetail}`);

        // Check auto-edit
        const isEditTool = AUTO_EDIT_PATTERNS.some((p) =>
          toolName.toLowerCase().includes(p)
        );
        if (isAutoEditEnabled() && isEditTool) {
          console.log(`[auto-edit] Auto-approving: ${toolName}`);
          sendControlResponse(slot, msg.request_id, {
            behavior: "allow",
            updatedInput: toolInput,
          });
          updateState(slot, {
            state: "working",
            message: `Auto-approved: ${toolName}`,
            promptToolType: undefined,
            promptToolDetail: undefined,
            promptDescription: undefined,
          });
          return;
        }

        // Store pending permission
        pendingPermissions.set(msg.request_id, {
          requestId: msg.request_id,
          slot,
          toolName,
          toolInput,
          toolUseId,
          description,
          permissionSuggestions,
        });

        updateState(slot, {
          state: "waiting",
          message: `${toolName}: ${toolDetail}`,
          promptToolType: toolName,
          promptToolDetail: toolDetail,
          promptDescription: description,
        });
      }
      break;
    }

    case "result": {
      // Update contextWindow from modelUsage if available
      if (msg.modelUsage && conn.model) {
        const modelInfo = msg.modelUsage[conn.model];
        if (modelInfo?.contextWindow) {
          conn.contextWindow = modelInfo.contextWindow;
        }
      }

      if (msg.is_error && msg.subtype === "error_during_execution") {
        updateState(slot, { state: "error", message: msg.result || "Error during execution" });
      } else {
        // success, error_max_turns, error_max_budget_usd all go to idle
        updateState(slot, {
          state: "idle",
          message: "",
          promptToolType: undefined,
          promptToolDetail: undefined,
          promptDescription: undefined,
        });
      }
      break;
    }

    case "tool_progress": {
      const elapsed = Math.round(msg.elapsed_time_seconds);
      updateState(slot, { state: "working", message: `Running: ${msg.tool_name} (${elapsed}s)` });
      break;
    }

    case "auth_status": {
      if (msg.error) {
        updateState(slot, { state: "error", message: msg.error });
      }
      break;
    }

    // Silently discard
    case "keep_alive":
    case "stream_event":
    case "tool_use_summary":
      break;

    default:
      console.log(`[cli] unknown message type (slot ${slot}): ${msg.type}`);
      break;
  }
}

// --- Outbound messages to CLI ---

function sendControlResponse(
  slot: number,
  requestId: string,
  response: { behavior: "allow"; updatedInput: Record<string, unknown>; updatedPermissions?: SDKPermissionUpdate[] } |
             { behavior: "deny"; message: string }
) {
  const conn = cliConnections.get(slot);
  if (!conn) {
    console.warn(`[cli] sendControlResponse: no connection for slot ${slot}`);
    return;
  }

  let payload: SDKControlResponseAllow | SDKControlResponseDeny;

  if (response.behavior === "allow") {
    payload = {
      type: "control_response",
      response: {
        subtype: "success",
        request_id: requestId,
        response: {
          behavior: "allow",
          updatedInput: response.updatedInput,
          updatedPermissions: response.updatedPermissions,
        },
      },
    };
  } else {
    payload = {
      type: "control_response",
      response: {
        subtype: "success",
        request_id: requestId,
        response: {
          behavior: "deny",
          message: response.message,
        },
      },
    };
  }

  conn.ws.send(JSON.stringify(payload) + "\n");
}

export function sendUserMessage(slot: number, content: string) {
  const conn = cliConnections.get(slot);
  if (!conn) {
    console.warn(`[cli] sendUserMessage: no connection for slot ${slot}`);
    return;
  }

  // Immediately show working state (prevents 1-5s idle gap)
  updateState(slot, { state: "working", message: "Processing prompt..." });

  const payload: SDKUserMessage = {
    type: "user",
    message: { role: "user", content },
    parent_tool_use_id: null,
    session_id: conn.sessionId ?? "",
  };

  conn.ws.send(JSON.stringify(payload) + "\n");
}

export function sendInterrupt(slot: number) {
  const conn = cliConnections.get(slot);
  if (!conn) return;

  const payload: SDKInterruptRequest = {
    type: "control_request",
    request_id: crypto.randomUUID(),
    request: { subtype: "interrupt" },
  };

  conn.ws.send(JSON.stringify(payload) + "\n");
  updateState(slot, { state: "working", message: "Interrupting..." });
}

export function resolvePermission(slot: number, action: "yes" | "always" | "no") {
  // Find the pending permission for this slot
  let pending: PendingPermission | undefined;
  for (const [id, perm] of pendingPermissions) {
    if (perm.slot === slot) {
      pending = perm;
      break;
    }
  }

  if (!pending) {
    console.log(`[cli] resolvePermission: no pending permission for slot ${slot}`);
    return;
  }

  pendingPermissions.delete(pending.requestId);

  if (action === "yes") {
    sendControlResponse(slot, pending.requestId, {
      behavior: "allow",
      updatedInput: pending.toolInput,
    });
  } else if (action === "always") {
    const updatedPermissions: SDKPermissionUpdate[] = pending.permissionSuggestions ?? [{
      type: "addRules",
      rules: [{ toolName: pending.toolName }],
      behavior: "allow",
      destination: "session",
    }];
    sendControlResponse(slot, pending.requestId, {
      behavior: "allow",
      updatedInput: pending.toolInput,
      updatedPermissions,
    });
  } else {
    sendControlResponse(slot, pending.requestId, {
      behavior: "deny",
      message: "Denied by user via rAI3DS",
    });
  }

  updateState(slot, {
    state: "working",
    promptToolType: undefined,
    promptToolDetail: undefined,
    promptDescription: undefined,
  });
}

export function cancelPendingPermissions(slot: number) {
  for (const [id, perm] of pendingPermissions) {
    if (perm.slot === slot) pendingPermissions.delete(id);
  }
}

export function getCliConnection(slot: number): CLIConnection | undefined {
  return cliConnections.get(slot);
}

export function getCliSessionId(slot: number): string | null {
  return cliConnections.get(slot)?.sessionId ?? null;
}

// --- Helpers ---

function findSlotForWs(ws: ServerWebSocket<WSData>): number | undefined {
  for (const [slot, conn] of cliConnections) {
    if (conn.ws === ws) return slot;
  }
  return undefined;
}

function extractToolDetail(input: Record<string, unknown>): string {
  const keys = ["command", "file_path", "pattern", "query", "url"];
  for (const key of keys) {
    if (typeof input[key] === "string") return input[key] as string;
  }
  for (const val of Object.values(input)) {
    if (typeof val === "string") return val;
  }
  return "";
}
