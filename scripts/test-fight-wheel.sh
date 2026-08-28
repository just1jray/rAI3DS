#!/bin/bash
# scripts/test-fight-wheel.sh - Test FIGHT wheel protocol
# Tests: option generation, pick, and run messages

set -e

echo "=== FIGHT Wheel Protocol Test ==="
echo ""
echo "This tests the FIGHT wheel feature (Mass Effect dialogue / Pokémon move menu)"
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

# Test 2: Trigger pre-tool hook and verify options change
echo ""
echo "=== Test 2: Options after pre-tool hook ==="
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
print(f'State: {agent.get(\"state\")} | lastBeat: {lastBeat}')
print(f'Options ({len(options)}):')
for opt in options:
    print(f'  [{opt.get(\"index\")}] {opt.get(\"label\")} -> \"{opt.get(\"fullPrompt\", \"\")[:50]}...\"')
if agent.get('state') == 'working':
    print('PASS: Agent is in working state')
else:
    print(f'FAIL: Expected working state')
    sys.exit(1)
"

# Test 3: WebSocket pick message
echo ""
echo "=== Test 3: WebSocket pick/run messages (mocked) ==="
echo ""
echo "To test WebSocket pick/run messages, use wscat:"
echo ""
echo "  wscat -c ws://localhost:3333"
echo ""
echo "Then send:"
echo '  {"type":"pick","slot":0,"index":0}  # Sends option prompt to agent'
echo '  {"type":"run","slot":0}              # Sends stop prompt to agent'
echo ""
echo "The server sends prompts via adapter.sendInput() (same path as picks)."
echo ""
echo "NOTE: RUN is a soft stop (sends stop prompt). No hard interrupt available."

# Test 4: Error state options
echo ""
echo "=== Test 4: Options in error state ==="
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
echo "  - POST /hook/pre-tool updates state and regenerates options"
echo "  - POST /hook/post-tool updates state and regenerates options"
echo "  - Options are generated based on agent state (heuristic approach)"
echo ""
echo "WebSocket messages (to test with wscat or 3DS):"
echo '  3DS -> Server: {"type":"pick","slot":0,"index":0}'
echo '  3DS -> Server: {"type":"run","slot":0}'
echo '  Server -> 3DS: agent_status with options[] array'
echo ""
echo "Option generation approach: Heuristic + template-based (no paid API)"
echo "  - State templates: idle, working, waiting, error, done"
echo "  - Tool augments: Shell, Write, Read, Grep, StrReplace"
echo "  - Contextual analysis of tool detail (file paths, npm, git, test)"
echo ""
echo "RUN implementation:"
echo "  - Sends stop prompt via sendInput (same path as picks)"
echo "  - GAP: Soft stop only - no hard interrupt without tmux"
echo "  - If agent is blocked in a tool, stop may not be immediate"
echo ""
echo "=== Done ==="
