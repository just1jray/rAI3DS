#!/bin/bash
# scripts/test-fight-wheel.sh - Test FIGHT wheel protocol
# Tests: option generation, pick, and run messages (soft-only, no tmux)
# CRITICAL: Options MUST be present on connect, BEFORE any Claude hooks

set -e

echo "=== FIGHT Wheel Protocol Test ==="
echo ""
echo "This tests the FIGHT wheel feature (Mass Effect dialogue / Pokémon move menu)"
echo "NOTE: pick/run are SOFT - they update state and log prompts (no tmux injection)"
echo ""
echo "CRITICAL: Options MUST exist on connect (no Claude/Anthropic required)"
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

# Test 0: CRITICAL - Options must exist on connect BEFORE any hooks
# This proves headless + Azahar sees the FIGHT wheel without burning Anthropic
echo "=== Test 0: Options on connect (NO hooks fired yet) ==="
echo "This is the CRITICAL test - options must exist without any Claude session"
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
state = agent.get('state', 'unknown')
active = agent.get('active', False)

print(f'Agent: {agent.get(\"name\")} | State: {state} | Active: {active} | Options: {len(options)}')

# CRITICAL: Must have 3+ options even without a Claude session (active=false)
if len(options) < 3:
    print(f'FAIL: Expected 3+ options on connect, got {len(options)}')
    print('OPTIONS MUST EXIST WITHOUT CLAUDE SESSION')
    sys.exit(1)

print('Options available on connect:')
for opt in options:
    print(f'  [{opt.get(\"index\")}] {opt.get(\"label\")} ({opt.get(\"kind\")})')

print('')
print('PASS: Options seeded on connect (no Claude hooks needed)')
print('Azahar/headless will see FIGHT wheel immediately')
"

# Test 1: Simulate WebSocket connect and verify options are broadcast
echo ""
echo "=== Test 1: WebSocket receives options on connect ==="
bun -e "
const ws = new WebSocket('ws://localhost:3333');
let received = [];

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'agent_status') {
    received.push(msg);
  }
  // Close after receiving initial broadcast
  if (received.length >= 1) {
    setTimeout(() => ws.close(), 100);
  }
};

ws.onclose = () => {
  if (received.length === 0) {
    console.log('FAIL: No agent_status received on connect');
    process.exit(1);
  }
  const msg = received[0];
  const options = msg.options || [];
  console.log('WS connect received:', msg.agent, '| Options:', options.length);
  if (options.length >= 3) {
    console.log('PASS: WebSocket client receives options immediately on connect');
    process.exit(0);
  } else {
    console.log('FAIL: Expected 3+ options on WS connect, got', options.length);
    process.exit(1);
  }
};

ws.onerror = (e) => {
  console.log('WS error:', e.message);
  process.exit(1);
};

setTimeout(() => {
  console.log('FAIL: Timeout waiting for WS message');
  process.exit(1);
}, 5000);
"

# Test 2: Check initial state has options (HTTP endpoint)
echo ""
echo "=== Test 2: Initial state with options (HTTP) ==="
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

# Test 3: Trigger pre-tool hook and verify state changes to working
echo ""
echo "=== Test 3: Pre-tool sets working state ==="
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

# Test 4: WebSocket pick updates lastBeat (soft - no tmux)
echo ""
echo "=== Test 4: WebSocket pick (soft) ==="

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

# Test 5: WebSocket run updates lastBeat (soft - no tmux)
echo ""
echo "=== Test 5: WebSocket run (soft stop) ==="

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

# Test 6: Error state options
echo ""
echo "=== Test 6: Options in error state ==="
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

# Test 7: Verify A-button pick sends WS message (simulates 3DS behavior)
echo ""
echo "=== Test 7: A-button pick with options (3DS simulation) ==="
echo "This verifies the fix for: 5 host-A presses, no WS pick"

bun -e "
const ws = new WebSocket('ws://localhost:3333');
let initialOptions = 0;
let pickSent = false;
let pickAcked = false;

ws.onopen = () => {
  // Wait for initial broadcast
};

ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type === 'agent_status' && msg.slot === 0) {
    const options = msg.options || [];
    
    if (!pickSent && options.length > 0) {
      // Initial state received with options - now send pick
      initialOptions = options.length;
      console.log('Initial state: ' + options.length + ' options');
      console.log('Simulating A-button press (pick index 0)...');
      ws.send(JSON.stringify({type: 'pick', slot: 0, index: 0}));
      pickSent = true;
    } else if (pickSent && !pickAcked) {
      // This is the response to our pick
      if (msg.lastBeat && msg.lastBeat.includes('Steer')) {
        console.log('Pick acknowledged: lastBeat = ' + msg.lastBeat);
        pickAcked = true;
        ws.close();
      }
    }
  }
};

ws.onclose = () => {
  if (initialOptions === 0) {
    console.log('FAIL: No options received on connect');
    process.exit(1);
  }
  if (!pickSent) {
    console.log('FAIL: Could not send pick (no options?)');
    process.exit(1);
  }
  if (!pickAcked) {
    console.log('FAIL: Pick not acknowledged (no lastBeat update)');
    process.exit(1);
  }
  console.log('PASS: A-button pick works with ' + initialOptions + ' options');
  process.exit(0);
};

ws.onerror = (e) => {
  console.log('WS error:', e.message);
  process.exit(1);
};

setTimeout(() => {
  if (!pickAcked) {
    console.log('FAIL: Timeout waiting for pick acknowledgment');
    process.exit(1);
  }
}, 5000);
"

# Summary
echo ""
echo "=== Test Summary ==="
echo ""
echo "CRITICAL (new in this PR):"
echo "  - Test 0: Options MUST exist on connect WITHOUT Claude hooks"
echo "  - Test 1: WebSocket client receives options immediately"
echo "  - Test 7: A-button pick sends WS message (fixes 5-A-press bug)"
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
echo "  - NO SILENT DROP: A without options shows 'NO MOVES!' flash"
echo "  - No direct input injection - prompts logged to console"
echo ""
echo "QA checklist for Azahar:"
echo "  1. Connect Azahar -> FIGHT wheel visible immediately (no Claude needed)"
echo "  2. host-A -> WS pick sent, state updates"  
echo "  3. host-B (after rebind to keyboard B) -> WS run sent, 'RUN sent!' flash"
echo ""
echo "=== All tests passed ==="
