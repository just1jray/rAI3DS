// Agent types
export type AgentState = "working" | "waiting" | "idle" | "error" | "done";

// State stored per agent slot
export interface AgentStatus {
  name: string;
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

// PermissionRequest hook payload from Claude Code
// This is the primary hook for permission decisions (Aug 2026)
export interface PermissionRequestHook {
  session_id: string;
  tool_name: string;
  tool_input: Record<string, unknown>;
  tool_use_id: string;
  hook_event_name: "PermissionRequest";
  cwd?: string;
  permission_mode?: string;
}

// PermissionRequest response format
export interface PermissionRequestResponse {
  hookSpecificOutput: {
    hookEventName: "PermissionRequest";
    decision: {
      behavior: "allow" | "deny";
      message?: string;
    };
  };
}

// PreToolUse hook payload (fires before tool execution)
export interface PreToolHook {
  session_id?: string;
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  tool_use_id?: string;
  hook_event_name?: string;
  // Legacy field from old hook format
  tool?: string;
}

// PostToolUse hook payload
export interface PostToolHook {
  session_id?: string;
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  tool_use_id?: string;
  // Legacy fields
  tool?: string;
  output?: string;
  error?: string;
}

// Lifecycle hook payloads
export interface LifecycleHook {
  session_id?: string;
}

export type SessionStartHook = LifecycleHook & {
  hook_event_name?: "SessionStart";
};
export type SessionEndHook = LifecycleHook & {
  hook_event_name?: "SessionEnd";
};
export type StopHook = LifecycleHook;

export interface UserPromptHook extends LifecycleHook {
  prompt?: string;
}

// Messages to 3DS
export interface AgentStatusMessage {
  type: "agent_status";
  agent: string;
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
  agent: string;
  action: "yes" | "always" | "no" | "escape";
  slot?: number;
}

export interface UserCommand {
  type: "command";
  agent: string;
  command: string;
  slot?: number;
}

export interface UserConfig {
  type: "config";
  agent: string;
  autoEdit?: boolean;
}

export interface SpawnRequest {
  type: "spawn_request";
  slot?: number;
}

export type DSMessage = UserAction | UserCommand | UserConfig | SpawnRequest;

// Pending permission request (held HTTP response)
export interface PendingPermission {
  requestId: string;
  slot: number;
  toolName: string;
  toolInput: Record<string, unknown>;
  description: string;
  sessionId: string;
  createdAt: number;
  resolve: (response: PermissionRequestResponse) => void;
}
