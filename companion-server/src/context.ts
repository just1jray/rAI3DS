import { readdirSync, statSync } from "fs";
import { homedir } from "os";
import { join } from "path";
import { updateContextPercent } from "./server";

const PROJECT_DIR = join(
  homedir(),
  ".claude",
  "projects",
  `-${process.cwd().replace(/\//g, "-").slice(1)}`
);

const CONTEXT_WINDOW = 200_000;

interface UsageEntry {
  input_tokens?: number;
  cache_creation_input_tokens?: number;
  cache_read_input_tokens?: number;
}

function findLatestJsonl(): string | null {
  try {
    const entries = readdirSync(PROJECT_DIR);
    let latest: { path: string; mtime: number } | null = null;

    for (const entry of entries) {
      if (!entry.endsWith(".jsonl")) continue;
      const fullPath = join(PROJECT_DIR, entry);
      try {
        const stat = statSync(fullPath);
        if (!stat.isFile()) continue;
        if (!latest || stat.mtimeMs > latest.mtime) {
          latest = { path: fullPath, mtime: stat.mtimeMs };
        }
      } catch {
        continue;
      }
    }

    return latest?.path ?? null;
  } catch {
    return null;
  }
}

async function readLastLines(filePath: string, count: number): Promise<string[]> {
  try {
    const file = Bun.file(filePath);
    const text = await file.text();
    const lines = text.trimEnd().split("\n");
    return lines.slice(-count);
  } catch {
    return [];
  }
}

async function computeContextPercent(): Promise<number> {
  const jsonlPath = findLatestJsonl();
  if (!jsonlPath) return 0;

  const lines = await readLastLines(jsonlPath, 50);

  // Find the last assistant message with usage data
  let lastUsage: UsageEntry | null = null;

  for (const line of lines) {
    try {
      const entry = JSON.parse(line);
      if (entry.type === "assistant" && entry.message?.usage) {
        lastUsage = entry.message.usage;
      }
    } catch {
      continue;
    }
  }

  if (!lastUsage) return 0;

  const total =
    (lastUsage.input_tokens ?? 0) +
    (lastUsage.cache_creation_input_tokens ?? 0) +
    (lastUsage.cache_read_input_tokens ?? 0);

  return Math.min(100, Math.round((total / CONTEXT_WINDOW) * 100));
}

let intervalId: ReturnType<typeof setInterval> | null = null;

export function startContextTracker(intervalMs: number = 10_000) {
  console.log(`[context] Starting context tracker (every ${intervalMs / 1000}s)`);

  const tick = async () => {
    const percent = await computeContextPercent();
    updateContextPercent(percent);
  };

  // Run immediately, then on interval
  tick();
  intervalId = setInterval(tick, intervalMs);
}

export function stopContextTracker() {
  if (intervalId) {
    clearInterval(intervalId);
    intervalId = null;
  }
}
