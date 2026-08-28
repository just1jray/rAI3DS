/**
 * Cursor Cloud Agent API Adapter
 *
 * Provides communication with Cursor cloud agents via the public API.
 * Used by rAI3DS companion server to steer and monitor Cursor agents.
 *
 * API docs: https://cursor.com/docs/cloud-agent/api/endpoints
 */

const CURSOR_API_BASE = "https://api.cursor.com";

export interface CursorAgent {
  id: string; // bc-XXXX format
  name: string;
  status: "ACTIVE" | "IDLE" | "ARCHIVED";
  url: string;
  latestRunId?: string;
  createdAt: string;
  updatedAt: string;
  repos?: Array<{ url: string; startingRef?: string }>;
}

export interface CursorRun {
  id: string; // run-XXXX format
  agentId: string;
  status: "CREATING" | "RUNNING" | "FINISHED" | "ERROR" | "CANCELLED" | "EXPIRED";
  createdAt: string;
  updatedAt: string;
  durationMs?: number;
  result?: string;
  git?: {
    branches: Array<{
      repoUrl: string;
      branch?: string;
      prUrl?: string;
    }>;
  };
}

export interface CursorListAgentsResponse {
  items: CursorAgent[];
  nextCursor?: string;
}

export interface CursorCreateRunResponse {
  run: CursorRun;
}

export interface CursorAdapterConfig {
  apiKey?: string;
  baseUrl?: string;
}

/**
 * Cursor Cloud Agent adapter for rAI3DS
 * Enables the 3DS to steer and monitor Cursor agents via FIGHT wheel
 */
export class CursorAdapter {
  private apiKey: string | undefined;
  private baseUrl: string;

  constructor(config: CursorAdapterConfig = {}) {
    this.apiKey = config.apiKey || process.env.CURSOR_API_KEY;
    this.baseUrl = config.baseUrl || CURSOR_API_BASE;
  }

  /**
   * Check if the adapter is configured with an API key
   */
  isConfigured(): boolean {
    return !!this.apiKey;
  }

  /**
   * Get authentication headers for API requests
   */
  private getAuthHeaders(): Record<string, string> {
    if (!this.apiKey) {
      throw new Error("CURSOR_API_KEY not configured");
    }
    const auth = Buffer.from(`${this.apiKey}:`).toString("base64");
    return {
      Authorization: `Basic ${auth}`,
      "Content-Type": "application/json",
    };
  }

  /**
   * List available Cursor cloud agents
   * Returns agents newest first
   */
  async listAgents(options: {
    limit?: number;
    cursor?: string;
    includeArchived?: boolean;
  } = {}): Promise<CursorListAgentsResponse> {
    const params = new URLSearchParams();
    if (options.limit) params.set("limit", String(options.limit));
    if (options.cursor) params.set("cursor", options.cursor);
    if (options.includeArchived !== undefined) {
      params.set("includeArchived", String(options.includeArchived));
    }

    const url = `${this.baseUrl}/v1/agents${params.toString() ? `?${params}` : ""}`;
    const response = await fetch(url, {
      method: "GET",
      headers: this.getAuthHeaders(),
    });

    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to list agents: ${response.status} ${error}`);
    }

    return response.json();
  }

  /**
   * Get details for a specific agent
   */
  async getAgent(agentId: string): Promise<CursorAgent> {
    const url = `${this.baseUrl}/v1/agents/${agentId}`;
    const response = await fetch(url, {
      method: "GET",
      headers: this.getAuthHeaders(),
    });

    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to get agent ${agentId}: ${response.status} ${error}`);
    }

    return response.json();
  }

  /**
   * Get the latest run for an agent
   */
  async getLatestRun(agentId: string): Promise<CursorRun | null> {
    const url = `${this.baseUrl}/v1/agents/${agentId}/runs?limit=1`;
    const response = await fetch(url, {
      method: "GET",
      headers: this.getAuthHeaders(),
    });

    if (!response.ok) {
      const error = await response.text();
      throw new Error(`Failed to get runs for ${agentId}: ${response.status} ${error}`);
    }

    const data = await response.json() as { items: CursorRun[] };
    return data.items[0] || null;
  }

  /**
   * Send a follow-up prompt to steer a Cursor agent
   * This is the key method for FIGHT wheel steering
   *
   * @param agentId - The agent to steer (bc-XXXX format)
   * @param prompt - The steering prompt to send
   * @returns The new run created for this follow-up
   * @throws If agent is busy (409) or other error
   */
  async steer(agentId: string, prompt: string): Promise<CursorCreateRunResponse> {
    const url = `${this.baseUrl}/v1/agents/${agentId}/runs`;
    const response = await fetch(url, {
      method: "POST",
      headers: this.getAuthHeaders(),
      body: JSON.stringify({
        prompt: { text: prompt },
      }),
    });

    if (!response.ok) {
      const error = await response.text();
      if (response.status === 409) {
        throw new Error(`Agent ${agentId} is busy - another run is active`);
      }
      throw new Error(`Failed to steer agent ${agentId}: ${response.status} ${error}`);
    }

    return response.json();
  }

  /**
   * Cancel the active run for an agent (soft stop)
   *
   * @param agentId - The agent to stop
   * @param runId - The run to cancel
   */
  async cancelRun(agentId: string, runId: string): Promise<{ id: string }> {
    const url = `${this.baseUrl}/v1/agents/${agentId}/runs/${runId}/cancel`;
    const response = await fetch(url, {
      method: "POST",
      headers: this.getAuthHeaders(),
    });

    if (!response.ok) {
      const error = await response.text();
      if (response.status === 409) {
        // run_not_cancellable - already terminal or never active
        console.log(`[cursor] Run ${runId} not cancellable (already terminal)`);
        return { id: runId };
      }
      throw new Error(`Failed to cancel run ${runId}: ${response.status} ${error}`);
    }

    return response.json();
  }

  /**
   * Stop the agent by canceling its latest active run
   * Returns true if a run was cancelled, false if no active run
   */
  async stop(agentId: string): Promise<boolean> {
    const run = await this.getLatestRun(agentId);
    if (!run) {
      console.log(`[cursor] No runs found for agent ${agentId}`);
      return false;
    }

    if (run.status !== "CREATING" && run.status !== "RUNNING") {
      console.log(`[cursor] Agent ${agentId} run ${run.id} already ${run.status}`);
      return false;
    }

    await this.cancelRun(agentId, run.id);
    console.log(`[cursor] Cancelled run ${run.id} for agent ${agentId}`);
    return true;
  }

  /**
   * Map Cursor agent/run status to rAI3DS AgentState
   */
  mapToAgentState(agent: CursorAgent, run?: CursorRun | null): {
    state: "idle" | "working" | "waiting" | "error" | "done";
    message: string;
  } {
    // No run = idle
    if (!run) {
      return {
        state: agent.status === "ARCHIVED" ? "done" : "idle",
        message: agent.status === "ARCHIVED" ? "Agent archived" : "Ready",
      };
    }

    switch (run.status) {
      case "CREATING":
        return { state: "working", message: "Starting run..." };
      case "RUNNING":
        return { state: "working", message: "Agent working..." };
      case "FINISHED":
        return {
          state: "done",
          message: run.result ? run.result.slice(0, 100) : "Run completed",
        };
      case "ERROR":
        return { state: "error", message: "Run failed" };
      case "CANCELLED":
        return { state: "idle", message: "Run cancelled" };
      case "EXPIRED":
        return { state: "idle", message: "Run expired" };
      default:
        return { state: "idle", message: "Unknown state" };
    }
  }
}

/**
 * Mock adapter for testing without API access
 * Simulates Cursor agent behavior for development
 */
export class MockCursorAdapter extends CursorAdapter {
  private mockAgents: Map<string, CursorAgent> = new Map();
  private mockRuns: Map<string, CursorRun> = new Map();
  private steerLog: Array<{ agentId: string; prompt: string; timestamp: number }> = [];
  private stopLog: Array<{ agentId: string; timestamp: number }> = [];

  constructor() {
    super({ apiKey: "mock-key" });

    // Initialize with a mock agent
    const mockAgent: CursorAgent = {
      id: "bc-mock-0000-0000-0000-000000000001",
      name: "Mock Cursor Agent",
      status: "ACTIVE",
      url: "https://cursor.com/agents/bc-mock-0000-0000-0000-000000000001",
      latestRunId: "run-mock-0000-0000-0000-000000000001",
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString(),
    };
    this.mockAgents.set(mockAgent.id, mockAgent);

    const mockRun: CursorRun = {
      id: "run-mock-0000-0000-0000-000000000001",
      agentId: mockAgent.id,
      status: "RUNNING",
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString(),
    };
    this.mockRuns.set(mockRun.id, mockRun);
  }

  isConfigured(): boolean {
    return true; // Mock is always "configured"
  }

  async listAgents(): Promise<CursorListAgentsResponse> {
    return { items: Array.from(this.mockAgents.values()) };
  }

  async getAgent(agentId: string): Promise<CursorAgent> {
    const agent = this.mockAgents.get(agentId);
    if (!agent) {
      throw new Error(`Mock agent ${agentId} not found`);
    }
    return agent;
  }

  async getLatestRun(agentId: string): Promise<CursorRun | null> {
    const agent = this.mockAgents.get(agentId);
    if (!agent?.latestRunId) return null;
    return this.mockRuns.get(agent.latestRunId) || null;
  }

  async steer(agentId: string, prompt: string): Promise<CursorCreateRunResponse> {
    console.log(`[mock-cursor] STEER ${agentId}: "${prompt}"`);
    this.steerLog.push({ agentId, prompt, timestamp: Date.now() });

    const runId = `run-mock-${Date.now()}`;
    const run: CursorRun = {
      id: runId,
      agentId,
      status: "RUNNING",
      createdAt: new Date().toISOString(),
      updatedAt: new Date().toISOString(),
    };
    this.mockRuns.set(runId, run);

    const agent = this.mockAgents.get(agentId);
    if (agent) {
      agent.latestRunId = runId;
      agent.updatedAt = new Date().toISOString();
    }

    return { run };
  }

  async stop(agentId: string): Promise<boolean> {
    console.log(`[mock-cursor] STOP ${agentId}`);
    this.stopLog.push({ agentId, timestamp: Date.now() });

    const agent = this.mockAgents.get(agentId);
    if (!agent?.latestRunId) return false;

    const run = this.mockRuns.get(agent.latestRunId);
    if (run && (run.status === "CREATING" || run.status === "RUNNING")) {
      run.status = "CANCELLED";
      run.updatedAt = new Date().toISOString();
      return true;
    }
    return false;
  }

  /**
   * Get the steer log for testing
   */
  getSteerLog() {
    return this.steerLog;
  }

  /**
   * Get the stop log for testing
   */
  getStopLog() {
    return this.stopLog;
  }

  /**
   * Add a mock agent for testing
   */
  addMockAgent(agent: CursorAgent, run?: CursorRun) {
    this.mockAgents.set(agent.id, agent);
    if (run) {
      this.mockRuns.set(run.id, run);
    }
  }
}

/**
 * Create the appropriate adapter based on configuration
 * Uses mock adapter if no API key is set and mock mode is enabled
 */
export function createCursorAdapter(options: {
  useMock?: boolean;
  apiKey?: string;
} = {}): CursorAdapter {
  const apiKey = options.apiKey || process.env.CURSOR_API_KEY;

  if (options.useMock || (!apiKey && process.env.CURSOR_MOCK === "1")) {
    console.log("[cursor] Using mock adapter (no API key or CURSOR_MOCK=1)");
    return new MockCursorAdapter();
  }

  if (!apiKey) {
    console.log("[cursor] No CURSOR_API_KEY set - Cursor slots will be unavailable");
    return new CursorAdapter(); // Returns unconfigured adapter
  }

  console.log("[cursor] Using live Cursor Cloud API adapter");
  return new CursorAdapter({ apiKey });
}
