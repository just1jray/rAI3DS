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

// Hook payloads from Claude Code
export interface PreToolHook {
  session_id?: string;
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  // Legacy field from old hook format
  tool?: string;
}

export interface PostToolHook {
  session_id?: string;
  tool_name?: string;
  tool_input?: Record<string, unknown>;
  // Legacy fields
  tool?: string;
  output?: string;
  error?: string;
}

// Lifecycle hook payloads (all share the same shape)
export interface LifecycleHook {
  session_id?: string;
}

export type SessionStartHook = LifecycleHook;
export type SessionEndHook = LifecycleHook;
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
  slot: number;
}

export type DSMessage = UserAction | UserCommand | UserConfig | SpawnRequest;
