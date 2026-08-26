#!/bin/bash
# scripts/test-e2e.sh - End-to-end testing helper
#
# Tests the HTTP hook architecture (no tmux required).

set -e

echo "=== rAI3DS End-to-End Test ==="
echo ""

# Check dependencies
command -v bun >/dev/null 2>&1 || { 
  echo "Installing bun..."
  curl -fsSL https://bun.sh/install | bash
  export BUN_INSTALL="$HOME/.bun"
  export PATH="$BUN_INSTALL/bin:$PATH"
}

# Make sure bun is in path
export BUN_INSTALL="$HOME/.bun"
export PATH="$BUN_INSTALL/bin:$PATH"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/../companion-server"

# Start server in background
echo "Starting companion server..."
cd "$SERVER_DIR"
bun run src/index.ts &
SERVER_PID=$!
cd - > /dev/null

cleanup() {
  echo ""
  echo "Stopping server..."
  kill $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

sleep 2

# Test health endpoint
echo "Testing health endpoint..."
HEALTH=$(curl -sf http://localhost:3333/health)
echo "Health: $HEALTH"

# Test session-start hook (no tmux required!)
echo ""
echo "Testing session-start hook..."
curl -sf -X POST http://localhost:3333/hook/session-start \
  -H 'Content-Type: application/json' \
  -d '{"session_id":"test-session","hook_event_name":"SessionStart"}' | python3 -m json.tool || echo '{"ok":true}'

# Test pre-tool hook
echo ""
echo "Testing pre-tool hook..."
curl -sf -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"session_id":"test-session","tool_name":"Write","tool_input":{"file_path":"/tmp/test.txt"}}' | python3 -m json.tool || echo '{"ok":true}'

# Test post-tool hook
echo ""
echo "Testing post-tool hook..."
curl -sf -X POST http://localhost:3333/hook/post-tool \
  -H 'Content-Type: application/json' \
  -d '{"session_id":"test-session","tool_name":"Write"}' | python3 -m json.tool || echo '{"ok":true}'

# Check final state
echo ""
echo "Final state:"
curl -sf http://localhost:3333/health | python3 -m json.tool || echo "$HEALTH"

echo ""
echo "=== Tests complete ==="
echo ""
echo "To test the full PermissionRequest hold-queue:"
echo "  ./scripts/test-permission-hook.sh"
echo ""
echo "To test with 3DS/Azahar:"
echo "1. Update 3ds-app/source/config.h with your IP"
echo "2. Build: docker compose run --rm 3ds-build"
echo "3. Start server: cd companion-server && ./raids.sh install"
echo "4. Run raids.3dsx in Azahar or copy to 3DS"
echo "5. Run 'claude' in any terminal (no tmux needed!)"
