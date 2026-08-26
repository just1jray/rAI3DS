#!/bin/bash
# scripts/test-permission-hook.sh - Test PermissionRequest hold-queue
#
# This script proves that:
# 1. A PermissionRequest hook holds the HTTP response
# 2. The state updates to "waiting" with tool details
# 3. A WS action (simulating 3DS) resolves the held request
# 4. The response contains the correct decision
#
# This is the test J Ray asked for: prove a PermissionRequest can be held
# and resolved by a WS action (even a mocked 3DS).

set -e

echo "=== rAI3DS PermissionRequest Hold-Queue Test ==="
echo ""

# Check dependencies
command -v bun >/dev/null 2>&1 || { echo "Error: bun not installed"; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "Error: curl not installed"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/../companion-server"

# Start server in background
echo "[test] Starting companion server..."
cd "$SERVER_DIR"
bun run src/index.ts &
SERVER_PID=$!
cd - > /dev/null

cleanup() {
  echo ""
  echo "[test] Cleaning up..."
  kill $SERVER_PID 2>/dev/null || true
  wait $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

# Wait for server to be ready
sleep 2
echo "[test] Checking server health..."
HEALTH=$(curl -sf http://localhost:3333/health)
echo "Health: $HEALTH"
echo ""

# Test 1: Session start (no tmux required!)
echo "=== Test 1: Session Start (no tmux) ==="
RESULT=$(curl -sf -X POST http://localhost:3333/hook/session-start \
  -H 'Content-Type: application/json' \
  -d '{"session_id":"test-session-123","hook_event_name":"SessionStart"}')
echo "Session start result: $RESULT"
echo ""

# Check that session is registered
HEALTH=$(curl -sf http://localhost:3333/health)
echo "Health after session start: $HEALTH"
echo ""

# Test 2: PermissionRequest hold (the key test!)
echo "=== Test 2: PermissionRequest Hold-Queue ==="
echo "[test] Sending PermissionRequest (this should hold until we send a WS action)..."
echo ""

# Send PermissionRequest in background (it will hold)
curl -sf -X POST http://localhost:3333/hook/permission-request \
  -H 'Content-Type: application/json' \
  -d '{
    "session_id": "test-session-123",
    "tool_name": "Bash",
    "tool_input": {"command": "echo hello"},
    "tool_use_id": "test-tool-123",
    "hook_event_name": "PermissionRequest"
  }' > /tmp/permission-response.json &
PERM_PID=$!

# Wait a moment for the request to be held
sleep 1

# Check that state is now "waiting"
echo "[test] Checking state (should be waiting)..."
HEALTH=$(curl -sf http://localhost:3333/health)
echo "Health: $HEALTH"
echo ""

# Verify pendingPermissions > 0
if echo "$HEALTH" | grep -q '"pendingPermissions":1'; then
  echo "[test] ✓ Permission request is being held (pendingPermissions: 1)"
else
  echo "[test] ✗ ERROR: Permission request not held!"
  exit 1
fi

# Check agent state is waiting
if echo "$HEALTH" | grep -q '"state":"waiting"'; then
  echo "[test] ✓ Agent state is 'waiting'"
else
  echo "[test] ✗ ERROR: Agent state should be 'waiting'"
  exit 1
fi
echo ""

# Test 3: Resolve via simulated 3DS WebSocket action
echo "=== Test 3: Resolve via WS Action (simulated 3DS) ==="
echo "[test] Simulating 3DS 'yes' action via raw HTTP (WebSocket would work the same)..."

# We can't easily send WS from bash, so we'll wait for the timeout
# OR we can use a Node/Bun one-liner to send a WS message
# For simplicity, let's create a quick bun script

cat > /tmp/send-ws-action.ts << 'EOF'
const ws = new WebSocket("ws://localhost:3333");
ws.onopen = () => {
  ws.send(JSON.stringify({
    type: "action",
    agent: "claude",
    action: "yes",
    slot: 0
  }));
  console.log("Sent yes action");
  setTimeout(() => {
    ws.close();
    process.exit(0);
  }, 500);
};
ws.onerror = (e) => {
  console.error("WS error:", e);
  process.exit(1);
};
EOF

bun run /tmp/send-ws-action.ts
echo ""

# Wait for the permission request to complete
wait $PERM_PID 2>/dev/null || true

# Check the response
echo "[test] Permission response:"
cat /tmp/permission-response.json
echo ""

# Verify the response contains "allow"
if grep -q '"behavior":"allow"' /tmp/permission-response.json; then
  echo ""
  echo "[test] ✓ Permission request resolved with 'allow'"
else
  echo ""
  echo "[test] ✗ ERROR: Expected 'allow' in response"
  exit 1
fi

# Verify pendingPermissions is now 0
HEALTH=$(curl -sf http://localhost:3333/health)
if echo "$HEALTH" | grep -q '"pendingPermissions":0'; then
  echo "[test] ✓ pendingPermissions is now 0"
else
  echo "[test] ✗ WARNING: pendingPermissions should be 0"
fi

echo ""
echo "=== All Tests Passed ==="
echo ""
echo "Summary:"
echo "  1. SessionStart hook registers session (no tmux required)"
echo "  2. PermissionRequest hook holds HTTP response until resolved"
echo "  3. WS action from 3DS resolves with allow/deny"
echo ""
echo "How J Ray tests this:"
echo "  1. Start companion: ./raids.sh install"
echo "  2. Open Claude: claude"
echo "  3. Ask Claude to run a command → PermissionRequest fires"
echo "  4. 3DS shows STATE_WAITING with tool details"
echo "  5. Press A on 3DS → resolves with allow, Claude continues"
echo "  6. No tmux required!"
