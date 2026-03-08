#!/usr/bin/env bash
#
# example.sh — Demonstrates the DriftNARS HTTP server.
#
# Prerequisites:
#   make httpd
#
# Usage:
#   ./examples/httpd/example.sh
#
set -euo pipefail

PORT=8181
SERVER=bin/driftnars-httpd
URL="http://127.0.0.1:${PORT}"

if [ ! -x "$SERVER" ]; then
    echo "Server binary not found. Run 'make httpd' first." >&2
    exit 1
fi

# Start the server in the background
"$SERVER" --port "$PORT" &
SERVER_PID=$!
trap 'kill $SERVER_PID 2>/dev/null; wait $SERVER_PID 2>/dev/null' EXIT

# Wait for it to be ready
for i in $(seq 1 20); do
    if curl -sf "${URL}/health" >/dev/null 2>&1; then break; fi
    sleep 0.1
done

echo "=== Health check ==="
curl -s "${URL}/health"
echo

echo ""
echo "=== DriftScript: deduction ==="
curl -s -X POST "${URL}/driftscript" -d '
(believe (inherit "robin" "bird"))
(believe (inherit "bird" "animal"))
(cycles 5)
(ask (inherit "robin" "animal"))
'

echo ""
echo "=== DriftScript: goal-driven action ==="
curl -s -X POST "${URL}/driftscript" -d '
(def-op ^press)
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
'

echo ""
echo "=== Raw Narsese ==="
curl -s -X POST "${URL}/narsese" -d '<cat --> animal>.
<cat --> pet>.
5
<cat --> ?1>?'

echo ""
echo "=== Reset ==="
curl -s -X POST "${URL}/reset"
echo

echo ""
echo "Done."
