#!/usr/bin/env bash
# rAI3DS launcher — starts companion server
# No tmux required! Sessions register via hooks when Claude Code starts.
#
# Usage:
#   raids              → start companion server (default)
#   raids install      → install hooks and start server
#   raids stop         → stop the companion server
#   raids help         → show help
#
# After starting the server, run Claude Code normally:
#   claude            → Claude registers via SessionStart hook
#   claude --resume   → resume works too
#
# The 3DS sees STATE_WAITING when Claude needs permission.
# Press A (yes), B (no), or X (always) on the 3DS.

set -euo pipefail

RAIDS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUN="${BUN:-$HOME/.bun/bin/bun}"
SERVER_SCRIPT="$RAIDS_DIR/src/index.ts"
LOG_FILE="/tmp/raids-server.log"
PID_FILE="/tmp/raids-server.pid"

stop_server() {
  if [ -f "$PID_FILE" ]; then
    local pid
    pid=$(cat "$PID_FILE")
    if kill -0 "$pid" 2>/dev/null; then
      echo "[raids] Stopping server (PID $pid)..."
      kill "$pid" 2>/dev/null || true
      rm -f "$PID_FILE"
      echo "[raids] Server stopped."
    else
      echo "[raids] Server not running (stale PID file)."
      rm -f "$PID_FILE"
    fi
  else
    echo "[raids] No PID file found. Server may not be running."
  fi
}

start_server() {
  local install_hooks="$1"

  # Check if server already running
  if curl -sf http://localhost:3333/health > /dev/null 2>&1; then
    echo "[raids] Companion server already running."
    return 0
  fi

  # Build command
  local cmd="$SERVER_SCRIPT"
  if [ "$install_hooks" = "true" ]; then
    cmd="$SERVER_SCRIPT install"
  fi

  echo "[raids] Starting companion server..."
  $BUN run $cmd > "$LOG_FILE" 2>&1 &
  local pid=$!
  echo $pid > "$PID_FILE"
  disown

  # Wait for server to be ready (up to 5 seconds)
  for i in $(seq 1 10); do
    if curl -sf http://localhost:3333/health > /dev/null 2>&1; then
      echo "[raids] Server ready (PID $pid)."
      echo ""
      echo "Now run Claude Code in any terminal:"
      echo "  claude"
      echo ""
      echo "Claude registers via SessionStart hook — no tmux needed."
      echo "The 3DS sees STATE_WAITING when Claude needs permission."
      return 0
    fi
    sleep 0.5
  done

  echo "[raids] ERROR: Server failed to start. Check $LOG_FILE"
  return 1
}

show_help() {
  echo "rAI3DS Companion Server"
  echo ""
  echo "Turn your Nintendo 3DS into a permission remote for Claude Code."
  echo "No tmux required — sessions register via hooks."
  echo ""
  echo "Usage:"
  echo "  raids              Start the companion server"
  echo "  raids install      Install hooks and start server"
  echo "  raids stop         Stop the companion server"
  echo "  raids help         Show this help"
  echo ""
  echo "After starting, run Claude Code normally:"
  echo "  claude             Claude registers via SessionStart hook"
  echo ""
  echo "When Claude needs permission:"
  echo "  - 3DS shows STATE_WAITING with tool details"
  echo "  - Press A (yes), B (no), or X (always)"
  echo "  - The HTTP hook responds, Claude continues"
  echo ""
  echo "Logs: $LOG_FILE"
}

# Parse command
cmd="${1:-start}"

case "$cmd" in
  help|--help|-h)
    show_help
    ;;
  stop)
    stop_server
    ;;
  install)
    start_server "true"
    ;;
  start|"")
    start_server "false"
    ;;
  *)
    echo "Unknown command: $cmd"
    echo "Run 'raids help' for usage."
    exit 1
    ;;
esac
