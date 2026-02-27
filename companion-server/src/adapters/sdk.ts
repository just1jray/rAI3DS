import { query, type Query, type PermissionResult, type SDKMessage, type SDKUserMessage, type PermissionUpdate } from "@anthropic-ai/claude-agent-sdk";
import type { UUID } from "crypto";

// Patterns that match edit/write operations for auto-edit
const AUTO_EDIT_PATTERNS = ["edit", "write", "notebook"];

// Pending permission request — created by canUseTool, resolved by 3DS user
export interface PendingPermission {
  toolUseID: string;
  toolName: string;
  toolInput: Record<string, unknown>;
  suggestions?: PermissionUpdate[];
  resolve: (result: PermissionResult) => void;
}

// Callbacks that the server wires up to broadcast state to 3DS
export interface SDKAdapterCallbacks {
  onStateChange(slot: number, state: "working" | "waiting" | "idle" | "error" | "done", message: string, toolInfo?: {
    toolType: string;
    toolDetail: string;
    description: string;
  }): void;
  onPermissionRequest(slot: number, toolName: string, toolInput: Record<string, unknown>, toolUseID: string): void;
  onContextUpdate(slot: number, percent: number): void;
  onSessionReady(slot: number, sessionId: string): void;
  isAutoEditEnabled(): boolean;
}

export interface SDKAdapter {
  slot: number;
  sessionId: string | null;
  status: "idle" | "active" | "waiting" | "done" | "error";

  /** Start a new Claude Code session with an initial prompt */
  start(initialPrompt?: string): void;
  /** Send a follow-up prompt to the running session */
  sendPrompt(text: string): void;
  /** Resolve a pending permission request */
  resolvePermission(toolUseID: string, action: "yes" | "always" | "no"): void;
  /** Interrupt the current query */
  interrupt(): void;
  /** Stop and clean up the session */
  stop(): void;
}

export function createSDKAdapter(
  slot: number,
  callbacks: SDKAdapterCallbacks,
  cwd?: string,
): SDKAdapter {
  let currentQuery: Query | null = null;
  let sessionId: string | null = null;
  let status: "idle" | "active" | "waiting" | "done" | "error" = "idle";
  const pendingPermissions = new Map<string, PendingPermission>();

  // Prompt stream for multi-turn conversations
  let promptResolve: ((msg: SDKUserMessage) => void) | null = null;
  let promptStreamDone = false;

  async function* createPromptStream(initialPrompt: string): AsyncGenerator<SDKUserMessage> {
    // Yield the initial prompt
    yield {
      type: "user",
      message: { role: "user", content: initialPrompt },
      parent_tool_use_id: null,
      session_id: "",
    };

    // Then yield follow-up prompts as they come in
    while (!promptStreamDone) {
      const msg = await new Promise<SDKUserMessage>((resolve) => {
        promptResolve = resolve;
      });
      promptResolve = null;
      yield msg;
    }
  }

  function extractToolDetail(toolInput: Record<string, unknown>): string {
    const keys = ["command", "file_path", "pattern", "query", "url"] as const;
    for (const key of keys) {
      if (typeof toolInput[key] === "string") return toolInput[key] as string;
    }
    const firstVal = Object.values(toolInput)[0];
    return typeof firstVal === "string" ? firstVal : "";
  }

  async function canUseTool(
    toolName: string,
    input: Record<string, unknown>,
    options: {
      signal: AbortSignal;
      suggestions?: PermissionUpdate[];
      blockedPath?: string;
      decisionReason?: string;
      toolUseID: string;
      agentID?: string;
    },
  ): Promise<PermissionResult> {
    const toolDetail = extractToolDetail(input);
    const description = typeof input.description === "string" ? input.description : "";

    // Auto-edit: if enabled and tool is an edit/write operation, auto-approve
    const isEditTool = AUTO_EDIT_PATTERNS.some((p) =>
      toolName.toLowerCase().includes(p),
    );
    if (callbacks.isAutoEditEnabled() && isEditTool) {
      console.log(`[sdk:${slot}] Auto-approving: ${toolName}`);
      callbacks.onStateChange(slot, "working", `Auto-approved: ${toolName}`);
      return { behavior: "allow" };
    }

    // Update state to waiting and notify 3DS
    status = "waiting";
    callbacks.onStateChange(slot, "waiting", `${toolName}: ${toolDetail}`, {
      toolType: toolName,
      toolDetail,
      description,
    });
    callbacks.onPermissionRequest(slot, toolName, input, options.toolUseID);

    // Wait for 3DS user to respond
    return new Promise<PermissionResult>((resolve) => {
      pendingPermissions.set(options.toolUseID, {
        toolUseID: options.toolUseID,
        toolName,
        toolInput: input,
        suggestions: options.suggestions,
        resolve,
      });

      // Also resolve on abort
      options.signal.addEventListener("abort", () => {
        pendingPermissions.delete(options.toolUseID);
        resolve({ behavior: "deny", message: "Aborted" });
      }, { once: true });
    });
  }

  async function processMessages(q: Query) {
    try {
      for await (const message of q) {
        handleMessage(message);
      }
    } catch (e) {
      console.error(`[sdk:${slot}] Message processing error:`, e);
      status = "error";
      callbacks.onStateChange(slot, "error", `Error: ${e}`);
    }
  }

  function handleMessage(message: SDKMessage) {
    switch (message.type) {
      case "assistant": {
        sessionId = message.session_id;
        status = "active";

        // Extract usage info for context tracking
        const usage = message.message?.usage;
        if (usage) {
          const inputTokens = (usage as any).input_tokens ?? 0;
          const cacheCreation = (usage as any).cache_creation_input_tokens ?? 0;
          const cacheRead = (usage as any).cache_read_input_tokens ?? 0;
          const total = inputTokens + cacheCreation + cacheRead;
          const percent = Math.min(100, Math.round((total / 200_000) * 100));
          callbacks.onContextUpdate(slot, percent);
        }

        // Check for tool use in content blocks
        const content = message.message?.content;
        if (Array.isArray(content)) {
          for (const block of content) {
            if (block.type === "tool_use") {
              callbacks.onStateChange(slot, "working", `Tool: ${block.name}`, {
                toolType: block.name,
                toolDetail: extractToolDetail((block.input ?? {}) as Record<string, unknown>),
                description: "",
              });
            } else if (block.type === "text" && block.text) {
              // Text output — agent is working
              callbacks.onStateChange(slot, "working", "Thinking...");
            }
          }
        }
        break;
      }

      case "result": {
        if (message.subtype === "success") {
          status = "done";
          callbacks.onStateChange(slot, "idle", message.result?.slice(0, 100) || "Done");
        } else {
          status = "error";
          callbacks.onStateChange(slot, "error", `Error: ${message.subtype}`);
        }
        break;
      }

      case "system": {
        if (message.subtype === "init") {
          sessionId = message.session_id;
          callbacks.onSessionReady(slot, message.session_id);
          callbacks.onStateChange(slot, "idle", "Session ready");
        }
        break;
      }

      case "tool_progress": {
        callbacks.onStateChange(slot, "working", `Running: ${message.tool_name}`);
        break;
      }
    }
  }

  return {
    get slot() { return slot; },
    get sessionId() { return sessionId; },
    get status() { return status; },

    start(initialPrompt?: string) {
      if (currentQuery) {
        console.warn(`[sdk:${slot}] Session already running`);
        return;
      }

      const prompt = initialPrompt || "Hello! I'm ready to work. What would you like me to do?";
      status = "active";
      callbacks.onStateChange(slot, "working", "Starting session...");

      const promptStream = createPromptStream(prompt);

      currentQuery = query({
        prompt: promptStream,
        options: {
          cwd: cwd || process.cwd(),
          canUseTool,
          systemPrompt: {
            type: "preset",
            preset: "claude_code",
          },
          tools: {
            type: "preset",
            preset: "claude_code",
          },
          settingSources: ["user", "project", "local"],
        },
      });

      // Process messages in background
      processMessages(currentQuery).then(() => {
        console.log(`[sdk:${slot}] Query completed`);
        status = "done";
        callbacks.onStateChange(slot, "done", "Session ended");
        currentQuery = null;
      }).catch((e) => {
        console.error(`[sdk:${slot}] Query error:`, e);
        status = "error";
        callbacks.onStateChange(slot, "error", `Session error: ${e}`);
        currentQuery = null;
      });
    },

    sendPrompt(text: string) {
      if (!promptResolve) {
        console.warn(`[sdk:${slot}] No active prompt stream to send to`);
        return;
      }
      const userMsg: SDKUserMessage = {
        type: "user",
        message: { role: "user", content: text },
        parent_tool_use_id: null,
        session_id: sessionId || "",
      };
      promptResolve(userMsg);
    },

    resolvePermission(toolUseID: string, action: "yes" | "always" | "no") {
      // If no specific toolUseID, resolve the most recent pending permission
      let pending: PendingPermission | undefined;
      if (toolUseID && pendingPermissions.has(toolUseID)) {
        pending = pendingPermissions.get(toolUseID);
      } else {
        // Resolve the first (oldest) pending permission
        const first = pendingPermissions.entries().next();
        if (!first.done) {
          pending = first.value[1];
        }
      }

      if (!pending) {
        console.warn(`[sdk:${slot}] No pending permission to resolve`);
        return;
      }

      pendingPermissions.delete(pending.toolUseID);

      let result: PermissionResult;
      switch (action) {
        case "yes":
          result = { behavior: "allow" };
          break;
        case "always":
          // Use suggestions to permanently allow this tool pattern
          result = {
            behavior: "allow",
            updatedPermissions: pending.suggestions,
          };
          break;
        case "no":
          result = { behavior: "deny", message: "User denied from 3DS" };
          break;
      }

      pending.resolve(result);
      status = "active";
      callbacks.onStateChange(slot, "working", `Approved: ${pending.toolName}`);
    },

    async interrupt() {
      if (currentQuery) {
        try {
          await currentQuery.interrupt();
          console.log(`[sdk:${slot}] Query interrupted`);
        } catch (e) {
          console.error(`[sdk:${slot}] Interrupt error:`, e);
        }
      }
    },

    stop() {
      promptStreamDone = true;

      // Reject all pending permissions
      for (const [, pending] of pendingPermissions) {
        pending.resolve({ behavior: "deny", message: "Session stopped" });
      }
      pendingPermissions.clear();

      if (currentQuery) {
        currentQuery.close();
        currentQuery = null;
      }

      // Reset prompt stream state for potential restart
      promptResolve = null;
      promptStreamDone = false;
      status = "done";
      console.log(`[sdk:${slot}] Session stopped`);
    },
  };
}
