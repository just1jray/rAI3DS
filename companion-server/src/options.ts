import type { FightOption, AgentStatus } from "./types";

/**
 * FIGHT Wheel Option Generator
 * 
 * Approach: Heuristic + template-based generation
 * No paid API required - generates options from:
 * - Current agent state (idle, working, waiting, error, done)
 * - Last tool used and its context
 * - Common steer patterns for coding agents
 * 
 * Options are short enough for 3DS bottom screen (~30 char labels)
 */

const MAX_OPTIONS = 6;
const MIN_OPTIONS = 3;

// Base templates for different agent states
const STATE_TEMPLATES: Record<string, FightOption[]> = {
  idle: [
    { index: 0, label: "Continue", fullPrompt: "Continue with the task", kind: "action" },
    { index: 1, label: "What's next?", fullPrompt: "What should we work on next?", kind: "question" },
    { index: 2, label: "Show progress", fullPrompt: "Show me what you've done so far", kind: "meta" },
    { index: 3, label: "Run tests", fullPrompt: "Run the tests to verify everything works", kind: "action" },
    { index: 4, label: "Commit changes", fullPrompt: "Commit the current changes with a good message", kind: "action" },
  ],
  working: [
    { index: 0, label: "Keep going", fullPrompt: "Keep going, looks good", kind: "steer" },
    { index: 1, label: "Explain approach", fullPrompt: "Explain your current approach", kind: "question" },
    { index: 2, label: "Show code", fullPrompt: "Show me the code you're writing", kind: "meta" },
    { index: 3, label: "Different approach", fullPrompt: "Try a different approach", kind: "steer" },
  ],
  error: [
    { index: 0, label: "Try again", fullPrompt: "Try again with a different approach", kind: "action" },
    { index: 1, label: "Show the error", fullPrompt: "Show me the full error message", kind: "meta" },
    { index: 2, label: "Debug this", fullPrompt: "Let's debug this step by step", kind: "steer" },
    { index: 3, label: "Revert changes", fullPrompt: "Revert the recent changes", kind: "action" },
    { index: 4, label: "Check logs", fullPrompt: "Check the logs for more context", kind: "action" },
  ],
  done: [
    { index: 0, label: "Great work!", fullPrompt: "Great work! What else can we improve?", kind: "steer" },
    { index: 1, label: "Run tests", fullPrompt: "Run the tests to make sure everything works", kind: "action" },
    { index: 2, label: "Review changes", fullPrompt: "Show me all the changes you made", kind: "meta" },
    { index: 3, label: "Push to remote", fullPrompt: "Push these changes to the remote", kind: "action" },
    { index: 4, label: "Next task", fullPrompt: "What should we tackle next?", kind: "question" },
  ],
  waiting: [
    { index: 0, label: "Proceed", fullPrompt: "Yes, proceed", kind: "action" },
    { index: 1, label: "Explain first", fullPrompt: "Explain what this will do first", kind: "question" },
    { index: 2, label: "Show diff", fullPrompt: "Show me the diff before proceeding", kind: "meta" },
  ],
};

// Cursor-specific templates (for cloud agents)
const CURSOR_STATE_TEMPLATES: Record<string, FightOption[]> = {
  idle: [
    { index: 0, label: "Continue", fullPrompt: "Continue with the task", kind: "action" },
    { index: 1, label: "What's next?", fullPrompt: "What should we work on next?", kind: "question" },
    { index: 2, label: "Show progress", fullPrompt: "Summarize your progress so far", kind: "meta" },
    { index: 3, label: "Create PR", fullPrompt: "Create a pull request for the changes", kind: "action" },
    { index: 4, label: "Run tests", fullPrompt: "Run the tests to verify everything works", kind: "action" },
  ],
  working: [
    { index: 0, label: "Keep going", fullPrompt: "Keep going, looks good", kind: "steer" },
    { index: 1, label: "Focus on X", fullPrompt: "Focus on the main functionality first", kind: "steer" },
    { index: 2, label: "Explain", fullPrompt: "Explain what you're doing", kind: "question" },
    { index: 3, label: "Pause here", fullPrompt: "Pause here and show me what you have", kind: "steer" },
    { index: 4, label: "Skip tests", fullPrompt: "Skip the tests for now, focus on implementation", kind: "steer" },
  ],
  error: [
    { index: 0, label: "Try again", fullPrompt: "Try a different approach", kind: "action" },
    { index: 1, label: "Show error", fullPrompt: "Show me the full error", kind: "meta" },
    { index: 2, label: "Debug", fullPrompt: "Debug this step by step", kind: "steer" },
    { index: 3, label: "Revert", fullPrompt: "Revert the failing changes", kind: "action" },
    { index: 4, label: "Skip this", fullPrompt: "Skip this for now and continue", kind: "steer" },
  ],
  done: [
    { index: 0, label: "Nice!", fullPrompt: "Nice work! Anything to improve?", kind: "steer" },
    { index: 1, label: "Create PR", fullPrompt: "Create a pull request", kind: "action" },
    { index: 2, label: "Run tests", fullPrompt: "Run the tests one more time", kind: "action" },
    { index: 3, label: "Review", fullPrompt: "Show me a summary of all changes", kind: "meta" },
    { index: 4, label: "New task", fullPrompt: "What should we work on next?", kind: "question" },
  ],
  waiting: [
    { index: 0, label: "Proceed", fullPrompt: "Yes, proceed", kind: "action" },
    { index: 1, label: "Explain", fullPrompt: "Explain what this will do", kind: "question" },
    { index: 2, label: "Show diff", fullPrompt: "Show me the diff first", kind: "meta" },
  ],
};

// Tool-specific options that can augment base templates
const TOOL_AUGMENTS: Record<string, FightOption[]> = {
  Write: [
    { index: 0, label: "Show the file", fullPrompt: "Show me the file you're writing", kind: "meta" },
    { index: 0, label: "Different path", fullPrompt: "Use a different file path", kind: "steer" },
  ],
  Read: [
    { index: 0, label: "Summarize it", fullPrompt: "Summarize what you found", kind: "question" },
    { index: 0, label: "Search deeper", fullPrompt: "Search for more related files", kind: "action" },
  ],
  Shell: [
    { index: 0, label: "Show output", fullPrompt: "Show me the command output", kind: "meta" },
    { index: 0, label: "Explain command", fullPrompt: "Explain what this command does", kind: "question" },
    { index: 0, label: "Try different cmd", fullPrompt: "Try a different command", kind: "steer" },
  ],
  Grep: [
    { index: 0, label: "Narrow search", fullPrompt: "Narrow the search criteria", kind: "steer" },
    { index: 0, label: "Expand search", fullPrompt: "Expand the search to more files", kind: "steer" },
  ],
  StrReplace: [
    { index: 0, label: "Preview change", fullPrompt: "Show me a preview of this change", kind: "meta" },
    { index: 0, label: "Check context", fullPrompt: "Show more context around the change", kind: "meta" },
  ],
  Task: [
    { index: 0, label: "Task status", fullPrompt: "What's the status of the subtask?", kind: "question" },
    { index: 0, label: "Help subtask", fullPrompt: "The subtask might need guidance", kind: "steer" },
  ],
};

// Context-based steers (from recent tool activity)
function generateContextualOptions(agent: AgentStatus): FightOption[] {
  const options: FightOption[] = [];
  
  // If there's a current tool, add tool-specific options
  if (agent.promptToolType) {
    const toolName = agent.promptToolType.split(/[^a-zA-Z]/)[0]; // Extract base tool name
    const augments = TOOL_AUGMENTS[toolName];
    if (augments && augments.length > 0) {
      options.push(augments[0]);
    }
    
    // If tool has detail, add contextual option
    if (agent.promptToolDetail) {
      const detail = agent.promptToolDetail;
      
      // File-related tools
      if (detail.includes("/") || detail.includes(".")) {
        options.push({
          index: 0,
          label: "Check file",
          fullPrompt: `Check the file: ${detail.slice(0, 50)}`,
          kind: "action",
        });
      }
      
      // Command-related
      if (detail.includes("npm") || detail.includes("bun") || detail.includes("yarn")) {
        options.push({
          index: 0,
          label: "Check package",
          fullPrompt: "Check the package.json for issues",
          kind: "action",
        });
      }
      
      // Git-related
      if (detail.includes("git")) {
        options.push({
          index: 0,
          label: "Git status",
          fullPrompt: "Show me git status",
          kind: "action",
        });
      }
      
      // Test-related
      if (detail.includes("test") || detail.includes("spec")) {
        options.push({
          index: 0,
          label: "Fix test",
          fullPrompt: "Fix the failing test",
          kind: "action",
        });
      }
    }
  }
  
  return options;
}

/**
 * Generate FIGHT wheel options for an agent
 * Returns 3-6 short options suitable for 3DS display
 */
export function generateOptions(agent: AgentStatus): FightOption[] {
  const state = agent.state || "idle";
  const source = agent.source || "claude";
  
  // Pick template set based on agent source
  const templates = source === "cursor" ? CURSOR_STATE_TEMPLATES : STATE_TEMPLATES;
  
  // Start with state-based templates
  const baseOptions = [...(templates[state] || templates.idle)];
  
  // Add contextual options (not for cursor agents without tool context)
  const contextual = source === "cursor" && !agent.promptToolType
    ? []
    : generateContextualOptions(agent);
  
  // Merge: put contextual first, then fill with base
  const merged: FightOption[] = [];
  const seen = new Set<string>();
  
  // Add contextual options first (more relevant)
  for (const opt of contextual) {
    if (merged.length >= MAX_OPTIONS) break;
    if (seen.has(opt.label.toLowerCase())) continue;
    seen.add(opt.label.toLowerCase());
    merged.push({ ...opt, index: merged.length });
  }
  
  // Fill remaining with base options
  for (const opt of baseOptions) {
    if (merged.length >= MAX_OPTIONS) break;
    if (seen.has(opt.label.toLowerCase())) continue;
    seen.add(opt.label.toLowerCase());
    merged.push({ ...opt, index: merged.length });
  }
  
  // Ensure minimum options
  while (merged.length < MIN_OPTIONS) {
    merged.push({
      index: merged.length,
      label: "Continue",
      fullPrompt: "Continue",
      kind: "steer",
    });
  }
  
  // Re-index
  return merged.map((opt, i) => ({ ...opt, index: i }));
}

/**
 * Truncate label for 3DS display (max ~28 chars)
 */
export function truncateLabel(label: string, maxLen: number = 28): string {
  if (label.length <= maxLen) return label;
  return label.slice(0, maxLen - 2) + "..";
}
