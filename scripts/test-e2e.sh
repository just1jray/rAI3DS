#!/bin/bash
# scripts/test-e2e.sh - End-to-end testing helper

set -e

echo "=== rAI3DS End-to-End Test ==="
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

# Test health endpoint
echo "Testing health endpoint..."
HEALTH=$(curl -s http://localhost:3333/health)
echo "Health: $HEALTH"

# Test hook endpoints
echo ""
echo "Testing pre-tool hook..."
curl -s -X POST http://localhost:3333/hook/pre-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}' | python3 -m json.tool

echo ""
echo "Testing waiting hook..."
curl -s -X POST http://localhost:3333/hook/waiting \
  -H 'Content-Type: application/json' \
  -d '{"command":"npm install express"}' | python3 -m json.tool

echo ""
echo "Testing post-tool hook..."
curl -s -X POST http://localhost:3333/hook/post-tool \
  -H 'Content-Type: application/json' \
  -d '{"tool":"Write"}' | python3 -m json.tool

# Check final state
echo ""
echo "Final state:"
curl -s http://localhost:3333/health | python3 -m json.tool

# Cleanup
echo ""
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null || true

echo ""
echo "=== Tests complete ==="
echo ""
echo "To test with 3DS/Citra:"
echo "1. Update 3ds-app/source/config.h with your IP"
echo "2. Build: docker compose run --rm 3ds-build"
echo "3. Start server: cd companion-server && bun run dev"
echo "4. Run raids.3dsx in Citra or copy to 3DS"
