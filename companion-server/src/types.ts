import type { ServerWebSocket } from "bun";

// Agent types (3DS protocol — unchanged)
export type AgentName = string;
export type AgentState = "working" | "waiting" | "idle" | "error" | "done";

export interface AgentStatus {
  name: AgentName;
  state: AgentState;
  progress: number; // 0-100, -1 for indeterminate
  message: string;
  lastUpdate: number;
  contextPercent: number; // 0-100
  promptToolType?: string;
  promptToolDetail?: string;
  promptDescription?: string;
  slot: number;           // 0-3 party position
  active: boolean;        // true if slot has a live session
}

// Messages to 3DS
export interface AgentStatusMessage {
  type: "agent_status";
  agent: AgentName;
  state: AgentState;
  progress: number;
  message: string;
  contextPercent?: number;
  promptToolType?: string;
  promptToolDetail?: string;
  promptDescription?: string;
  autoEdit?: boolean;
  slot: number;
  active: boolean;
}

export interface SpawnResultMessage {
  type: "spawn_result";
  slot: number;
  success: boolean;
  error?: string;
}

// Messages from 3DS
export interface UserAction {
  type: "action";
  agent: AgentName;
  action: "yes" | "always" | "no" | "escape";
  slot?: number;
}

export interface UserCommand {
  type: "command";
  agent: AgentName;
  command: string;
  slot?: number;
}

export interface UserConfig {
  type: "config";
  agent: AgentName;
  autoEdit?: boolean;
}

export interface SpawnRequest {
  type: "spawn_request";
  slot: number;
}

export type DSMessage = UserAction | UserCommand | UserConfig | SpawnRequest;

// --- SDK Incoming (CLI → Server) ---

export interface SDKSystemInit {
  type: "system";
  subtype: "init";
  session_id: string;
  model: string;
  cwd: string;
  tools: string[];
  permissionMode: string;
  claude_code_version: string;
  uuid: string;
}

export interface SDKAssistantMessage {
  type: "assistant";
  message: {
    id: string;
    role: "assistant";
    model: string;
    content: SDKContentBlock[];
    stop_reason: string | null;
    usage: SDKUsage;
  };
  session_id: string;
  error?: string;
  uuid: string;
}

export interface SDKContentBlock {
  type: "text" | "tool_use" | "tool_result" | "thinking";
  text?: string;
  id?: string;
  name?: string;
  input?: Record<string, unknown>;
  tool_use_id?: string;
  content?: string | SDKContentBlock[];
  is_error?: boolean;
  thinking?: string;
}

export interface SDKUsage {
  input_tokens: number;
  output_tokens: number;
  cache_creation_input_tokens: number;
  cache_read_input_tokens: number;
}

export interface SDKControlRequest {
  type: "control_request";
  request_id: string;
  request: {
    subtype: "can_use_tool";
    tool_name: string;
    input: Record<string, unknown>;
    tool_use_id: string;
    description?: string;
    permission_suggestions?: SDKPermissionUpdate[];
  };
}

export interface SDKPermissionUpdate {
  type: "addRules" | "replaceRules" | "removeRules" | "setMode";
  rules?: { toolName: string; ruleContent?: string }[];
  behavior?: "allow" | "deny" | "ask";
  destination: "session" | "projectSettings" | "userSettings" | "localSettings";
  mode?: string;
}

export interface SDKResult {
  type: "result";
  subtype: "success" | "error_during_execution" | "error_max_turns" | "error_max_budget_usd";
  is_error: boolean;
  result: string;
  duration_ms: number;
  num_turns: number;
  total_cost_usd: number;
  usage: SDKUsage;
  modelUsage?: Record<string, {
    inputTokens: number;
    outputTokens: number;
    contextWindow: number;
    maxOutputTokens: number;
    costUSD: number;
  }>;
  session_id: string;
  uuid: string;
}

export interface SDKToolProgress {
  type: "tool_progress";
  tool_name: string;
  tool_use_id: string;
  elapsed_time_seconds: number;
  uuid: string;
}

export interface SDKSystemStatus {
  type: "system";
  subtype: "status";
  status: "compacting" | null;
}

// --- SDK Outgoing (Server → CLI) ---

export interface SDKUserMessage {
  type: "user";
  message: { role: "user"; content: string };
  parent_tool_use_id: null;
  session_id: string;
}

export interface SDKControlResponseAllow {
  type: "control_response";
  response: {
    subtype: "success";
    request_id: string;
    response: {
      behavior: "allow";
      updatedInput: Record<string, unknown>;
      updatedPermissions?: SDKPermissionUpdate[];
    };
  };
}

export interface SDKControlResponseDeny {
  type: "control_response";
  response: {
    subtype: "success";
    request_id: string;
    response: {
      behavior: "deny";
      message: string;
    };
  };
}

export interface SDKInterruptRequest {
  type: "control_request";
  request_id: string;
  request: { subtype: "interrupt" };
}

// --- Internal state ---

export interface CLIConnection {
  ws: ServerWebSocket<WSData>;
  slot: number;
  sessionId: string | null;
  model: string | null;
  contextWindow: number;  // default 200_000, updated from result.modelUsage
}

export interface PendingPermission {
  requestId: string;
  slot: number;
  toolName: string;
  toolInput: Record<string, unknown>;
  toolUseId: string;
  description: string;
  permissionSuggestions?: SDKPermissionUpdate[];
}

export interface WSData {
  type: "cli" | "3ds";
  slot?: number;
}
