#!/usr/bin/env bash
# rAI3DS launcher — starts companion server + Claude Code in tmux
# Usage: raids [claude-args...]
#   raids              → start Claude Code
#   raids --resume     → resume last session
#   raids-stop         → kill server + tmux session

set -euo pipefail

RAIDS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUN="$HOME/.bun/bin/bun"
SERVER_SCRIPT="$RAIDS_DIR/src/index.ts"
TMUX_SESSION="claude-raids"
LOG_FILE="/tmp/raids-server.log"

# Start companion server if not already running
if ! curl -sf http://localhost:3333/health > /dev/null 2>&1; then
  echo "[raids] Starting companion server..."
  $BUN run "$SERVER_SCRIPT" install > "$LOG_FILE" 2>&1 &
  disown

  # Wait for server to be ready (up to 5 seconds)
  for i in $(seq 1 10); do
    if curl -sf http://localhost:3333/health > /dev/null 2>&1; then
      echo "[raids] Server ready."
      break
    fi
    sleep 0.5
  done

  if ! curl -sf http://localhost:3333/health > /dev/null 2>&1; then
    echo "[raids] ERROR: Server failed to start. Check $LOG_FILE"
    exit 1
  fi
else
  echo "[raids] Companion server already running."
fi

# Build the claude command with any extra args
CLAUDE_CMD="claude"
if [ $# -gt 0 ]; then
  CLAUDE_CMD="claude $*"
fi

# Create the tmux session if it doesn't exist
if ! tmux has-session -t "$TMUX_SESSION" 2>/dev/null; then
  echo "[raids] Creating tmux session '$TMUX_SESSION' with Claude Code..."
  tmux new-session -d -s "$TMUX_SESSION" "$CLAUDE_CMD"
fi

# Attach or switch to the session
if [ -n "${TMUX:-}" ]; then
  echo "[raids] Switching to session '$TMUX_SESSION'..."
  tmux switch-client -t "$TMUX_SESSION"
else
  echo "[raids] Attaching to session '$TMUX_SESSION'..."
  tmux attach-session -t "$TMUX_SESSION"
fi
