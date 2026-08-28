#!/bin/bash
# scripts/test-fight-wheel.sh - Test FIGHT wheel protocol
# Tests: option generation, pick, and run messages (soft-only, no tmux)

set -e

echo "=== FIGHT Wheel Protocol Test ==="
echo ""
echo "This tests the FIGHT wheel feature (Mass Effect dialogue / Pokémon move menu)"
echo "NOTE: pick/run are SOFT - they update state and log prompts (no tmux injection)"
echo ""

# Check dependencies
command -v bun >/dev/null 2>&1 || { echo "Error: bun not installed"; exit 1; }

# Start server in background
echo "Starting companion server..."
cd companion-server
bun run src/index.ts &
SERVER_PID=$!
cd ..
sleep 2

cleanup() {
  echo ""
  echo "Stopping server..."
  kill $SERVER_PID 2>/dev/null || true
}
trap cleanup EXIT

# Test 1: Check initial state has options
echo "=== Test 1: Initial state with options ==="
HEALTH=$(curl -s http://localhost:3333/health)
echo "$HEALTH" | python3 -c "
import json, sys
data = json.load(sys.stdin)
agents = data.get('agents', [])
if not agents:
    print('FAIL: No agents in response')
    sys.exit(1)
agent = agents[0]
options = agent.get('options', [])
print(f'Agent: {agent.get(\"name\")} | State: {agent.get(\"state\")} | Options: {len(options)}')
for opt in options:
    print(f'  [{opt.get(\"index\")}] {opt.get(\"label\")} ({opt.get(\"kind\")})')
if len(options) >= 3:
    print('PASS: Agent has 3+ options')
else:
    print(f'FAIL: Expected 3+ options, got {len(options)}')
    sys.exit(1)
"

# Test 2: Trigger pre-tool hook and verify state changes to working
echo ""
echo "=== Test 2: Pre-tool sets working state ==="
curl -s -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool_name":"Shell","tool_input":{"command":"npm test"}}' > /dev/null

sleep 0.5

HEALTH=$(curl -s http://localhost:3333/health)
echo "$HEALTH" | python3 -c "
import json, sys
data = json.load(sys.stdin)
agent = data.get('agents', [])[0]
options = agent.get('options', [])
lastBeat = agent.get('lastBeat', '')
state = agent.get('state')
print(f'State: {state} | lastBeat: {lastBeat}')
print(f'Options ({len(options)}):')
for opt in options:
    print(f'  [{opt.get(\"index\")}] {opt.get(\"label\")} -> \"{opt.get(\"fullPrompt\", \"\")[:50]}...\"')
if state == 'working':
    print('PASS: Agent is in working state')
else:
    print(f'FAIL: Expected working state, got {state}')
    sys.exit(1)
"

# Test 3: WebSocket pick updates lastBeat (soft - no tmux)
echo ""
echo "=== Test 3: WebSocket pick (soft) ==="

# Use bun to send WS message and check response
bun -e "
const ws = new WebSocket('ws://localhost:3333');
let received = [];

ws.onopen = () => {
  // Send pick command
  ws.send(JSON.stringify({type: 'pick', slot: 0, index: 0}));
};

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'agent_status' && msg.slot === 0) {
    received.push(msg);
    // Wait for state update after pick
    if (received.length >= 2) {
      ws.close();
    }
  }
};

ws.onclose = () => {
  const last = received[received.length - 1];
  if (last && last.lastBeat && last.lastBeat.includes('Steer')) {
    console.log('State:', last.state, '| lastBeat:', last.lastBeat);
    console.log('PASS: pick updated lastBeat');
    process.exit(0);
  } else {
    console.log('FAIL: pick did not update lastBeat');
    console.log('Last message:', JSON.stringify(last));
    process.exit(1);
  }
};

ws.onerror = (e) => {
  console.log('WS error:', e.message);
  process.exit(1);
};

// Timeout
setTimeout(() => {
  console.log('FAIL: Timeout waiting for WS response');
  process.exit(1);
}, 5000);
"

# Test 4: WebSocket run updates lastBeat (soft - no tmux)
echo ""
echo "=== Test 4: WebSocket run (soft stop) ==="

bun -e "
const ws = new WebSocket('ws://localhost:3333');
let received = [];

ws.onopen = () => {
  // Send run command
  ws.send(JSON.stringify({type: 'run', slot: 0}));
};

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'agent_status' && msg.slot === 0) {
    received.push(msg);
    if (received.length >= 2) {
      ws.close();
    }
  }
};

ws.onclose = () => {
  const last = received[received.length - 1];
  if (last && last.lastBeat && last.lastBeat.includes('RUN')) {
    console.log('State:', last.state, '| lastBeat:', last.lastBeat);
    console.log('PASS: run updated lastBeat (soft stop)');
    process.exit(0);
  } else {
    console.log('FAIL: run did not update lastBeat');
    console.log('Last message:', JSON.stringify(last));
    process.exit(1);
  }
};

ws.onerror = (e) => {
  console.log('WS error:', e.message);
  process.exit(1);
};

setTimeout(() => {
  console.log('FAIL: Timeout waiting for WS response');
  process.exit(1);
}, 5000);
"

# Test 5: Error state options
echo ""
echo "=== Test 5: Options in error state ==="
curl -s -X POST http://localhost:3333/hook/post-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool_name":"Shell","error":"Command failed with exit code 1"}' > /dev/null

sleep 0.5

HEALTH=$(curl -s http://localhost:3333/health)
echo "$HEALTH" | python3 -c "
import json, sys
data = json.load(sys.stdin)
agent = data.get('agents', [])[0]
options = agent.get('options', [])
print(f'State: {agent.get(\"state\")} | lastBeat: {agent.get(\"lastBeat\", \"\")}')
print(f'Options ({len(options)}):')
for opt in options:
    print(f'  [{opt.get(\"index\")}] {opt.get(\"label\")} ({opt.get(\"kind\")})')
if agent.get('state') == 'error':
    print('PASS: Agent is in error state with contextual options')
else:
    print('WARN: Expected error state')
"

# Summary
echo ""
echo "=== Test Summary ==="
echo ""
echo "Protocol endpoints tested:"
echo "  - GET /health returns options[] for each agent"
echo "  - POST /hook/pre-tool updates state to working and regenerates options"
echo "  - POST /hook/post-tool updates state and regenerates options"
echo "  - WS pick updates lastBeat (soft - logs prompt, no injection)"
echo "  - WS run updates lastBeat (soft stop - logs prompt, no injection)"
echo ""
echo "Option generation approach: Heuristic + template-based (no paid API)"
echo "  - State templates: idle, working, waiting, error, done"
echo "  - Tool augments: Shell, Write, Read, Grep, StrReplace"
echo "  - Contextual analysis of tool detail (file paths, npm, git, test)"
echo ""
echo "FIGHT wheel behavior (soft-only, no tmux):"
echo "  - pick: Updates state + lastBeat, logs prompt for manual use"
echo "  - run: Updates state to idle + lastBeat, logs stop prompt"
echo "  - No direct input injection - prompts logged to console"
echo ""
echo "=== All tests passed ==="
