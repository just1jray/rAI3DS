// Agent types
export type AgentName = "claude" | "codex" | "gemini" | "cursor";
export type AgentState = "working" | "waiting" | "idle" | "error" | "done";

// State stored per agent
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
}

// Messages from 3DS
export interface UserAction {
  type: "action";
  agent: AgentName;
  action: "yes" | "always" | "no" | "escape";
}

export interface UserCommand {
  type: "command";
  agent: AgentName;
  command: string;
}

export interface UserConfig {
  type: "config";
  agent: AgentName;
  autoEdit?: boolean;
}

export type DSMessage = UserAction | UserCommand | UserConfig;
