#!/usr/bin/env bash
#
# test_ops.sh — Tests the DriftNARS HTTP server operation callback API.
#
# Starts the server, spins up a tiny listener to receive callbacks,
# registers an operation, triggers it, and verifies the callback fires.
#
# Prerequisites:
#   make httpd
#
# Usage:
#   ./examples/httpd/test_ops.sh
#
set -euo pipefail

PORT=8182
CB_PORT=8183
SERVER=bin/driftnars-httpd
URL="http://127.0.0.1:${PORT}"
CB_URL="http://127.0.0.1:${CB_PORT}/exec"
CB_LOG=$(mktemp)
PASS=0
FAIL=0

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    kill "$LISTENER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    wait "$LISTENER_PID" 2>/dev/null || true
    rm -f "$CB_LOG"
}
trap cleanup EXIT

if [ ! -x "$SERVER" ]; then
    echo "FAIL: Server binary not found. Run 'make httpd' first." >&2
    exit 1
fi

check() {
    local name="$1" expected="$2" actual="$3"
    if echo "$actual" | grep -qF "$expected"; then
        echo "  PASS: $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $name"
        echo "    expected to contain: $expected"
        echo "    got: $actual"
        FAIL=$((FAIL + 1))
    fi
}

# ── Start callback listener (nc-based, reads one request) ──────────────────
# We use a background loop that writes received data to CB_LOG.
start_listener() {
    while true; do
        # macOS nc needs -l -p PORT, Linux needs -l -p PORT or -l PORT
        # Use a python one-liner for portability
        python3 -c "
import http.server, json, sys

class H(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(length).decode()
        with open('$CB_LOG', 'a') as f:
            f.write(body + '\n')
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'{\"status\":\"ok\"}')
    def log_message(self, *a): pass

s = http.server.HTTPServer(('127.0.0.1', $CB_PORT), H)
s.serve_forever()
" &
        LISTENER_PID=$!
        break
    done
}

start_listener
sleep 0.3

# ── Start DriftNARS server ─────────────────────────────────────────────────
"$SERVER" --port "$PORT" &
SERVER_PID=$!

for i in $(seq 1 20); do
    if curl -sf "${URL}/health" >/dev/null 2>&1; then break; fi
    sleep 0.1
done

echo "=== Test: ops list is initially empty ==="
RESULT=$(curl -s "${URL}/ops")
check "empty ops list" "[]" "$RESULT"

echo "=== Test: register ^press ==="
RESULT=$(curl -s -X POST "${URL}/ops/register" \
    -H 'Content-Type: application/json' \
    -d "{\"op\":\"^press\",\"callback_url\":\"${CB_URL}\",\"min_confidence\":0.6}")
check "register response" '"status":"registered"' "$RESULT"

echo "=== Test: ops list contains ^press ==="
RESULT=$(curl -s "${URL}/ops")
check "ops list has ^press" '"op":"^press"' "$RESULT"
check "ops list has callback_url" "callback_url" "$RESULT"

echo "=== Test: register duplicate updates in place ==="
RESULT=$(curl -s -X POST "${URL}/ops/register" \
    -H 'Content-Type: application/json' \
    -d "{\"op\":\"^press\",\"callback_url\":\"${CB_URL}\",\"min_confidence\":0.8}")
check "re-register response" '"status":"registered"' "$RESULT"
RESULT=$(curl -s "${URL}/ops")
# Should have exactly one op (no comma between entries)
COMMA_COUNT=$(echo "$RESULT" | tr -cd ',' | wc -c | tr -d ' ')
if [ "$COMMA_COUNT" -le 4 ]; then
    echo "  PASS: still one op"
    PASS=$((PASS + 1))
else
    echo "  FAIL: still one op (too many entries)"
    FAIL=$((FAIL + 1))
fi
check "updated min_confidence" '0.8000' "$RESULT"

echo "=== Test: register ^goto ==="
curl -s -X POST "${URL}/ops/register" \
    -H 'Content-Type: application/json' \
    -d "{\"op\":\"^goto\",\"callback_url\":\"${CB_URL}\",\"min_confidence\":0.5}" >/dev/null

echo "=== Test: delete ^goto ==="
RESULT=$(curl -s -X DELETE "${URL}/ops/^goto")
check "delete response" '"status":"unregistered"' "$RESULT"

echo "=== Test: delete non-existent op ==="
RESULT=$(curl -s -X DELETE "${URL}/ops/^nope")
check "not found" "Op not found" "$RESULT"

echo "=== Test: register with missing fields ==="
RESULT=$(curl -s -X POST "${URL}/ops/register" \
    -H 'Content-Type: application/json' \
    -d '{"op":"^x"}')
check "missing callback_url rejected" "op and callback_url are required" "$RESULT"

echo "=== Test: POST /config ==="
RESULT=$(curl -s -X POST "${URL}/config" \
    -H 'Content-Type: application/json' \
    -d '{"decision_threshold":0.65,"motorbabbling":0.0,"volume":0}')
check "config response" '"status":"configured"' "$RESULT"

echo "=== Test: trigger ^press execution and verify callback ==="
# Clear log
> "$CB_LOG"

# Teach the system: light_on then ^press leads to light_off, trigger it
curl -s -X POST "${URL}/driftscript" -d '
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
' >/dev/null

# Give the callback a moment to arrive
sleep 0.5

if [ -s "$CB_LOG" ]; then
    CB_BODY=$(cat "$CB_LOG")
    check "callback contains op" '"op":"^press"' "$CB_BODY"
    check "callback contains timestamp" 'timestamp_ms' "$CB_BODY"
    echo "  Callback payload: $CB_BODY"
else
    echo "  FAIL: no callback received"
    FAIL=$((FAIL + 1))
fi

echo "=== Test: POST /reset preserves execution handler ==="
curl -s -X POST "${URL}/reset" >/dev/null
# Re-register the op (engine ops are cleared on reset, but our registry persists)
curl -s -X POST "${URL}/ops/register" \
    -H 'Content-Type: application/json' \
    -d "{\"op\":\"^press\",\"callback_url\":\"${CB_URL}\",\"min_confidence\":0.6}" >/dev/null
> "$CB_LOG"
curl -s -X POST "${URL}/driftscript" -d '
(believe (predict (seq "light_on" (call ^press)) "light_off"))
(believe "light_on" :now)
(goal "light_off")
' >/dev/null
sleep 0.5
if [ -s "$CB_LOG" ]; then
    check "callback after reset" '"op":"^press"' "$(cat "$CB_LOG")"
else
    echo "  FAIL: no callback after reset"
    FAIL=$((FAIL + 1))
fi

echo "=== Test: POST /save ==="
SAVE_PATH="/tmp/driftnars_test_httpd_$$.dnar"
RESULT=$(curl -s -X POST "${URL}/save" \
    -H 'Content-Type: application/json' \
    -d "{\"path\":\"${SAVE_PATH}\"}")
check "save response" '"status":"saved"' "$RESULT"
if [ -f "$SAVE_PATH" ]; then
    echo "  PASS: save file created"
    PASS=$((PASS + 1))
else
    echo "  FAIL: save file not created"
    FAIL=$((FAIL + 1))
fi

echo "=== Test: POST /save with missing path ==="
RESULT=$(curl -s -X POST "${URL}/save" \
    -H 'Content-Type: application/json' \
    -d '{}')
check "save missing path rejected" '"path" field is required' "$RESULT"

echo "=== Test: teach, save, reset, load, query ==="
# Teach something new
curl -s -X POST "${URL}/driftscript" -d '
(believe (inherit "eagle" "bird"))
(believe (inherit "bird" "flyer"))
(cycles 5)
' >/dev/null

# Save
curl -s -X POST "${URL}/save" \
    -H 'Content-Type: application/json' \
    -d "{\"path\":\"${SAVE_PATH}\"}" >/dev/null

# Reset
curl -s -X POST "${URL}/reset" >/dev/null

# Load
RESULT=$(curl -s -X POST "${URL}/load" \
    -H 'Content-Type: application/json' \
    -d "{\"path\":\"${SAVE_PATH}\"}")
check "load response" '"status":"loaded"' "$RESULT"

# Query — should still know eagle-->flyer after load
RESULT=$(curl -s -X POST "${URL}/narsese" -d '<eagle --> flyer>?')
if echo "$RESULT" | grep -qF "Answer:"; then
    echo "  PASS: knowledge survived save/load"
    PASS=$((PASS + 1))
else
    echo "  FAIL: knowledge lost after save/load"
    echo "    got: $RESULT"
    FAIL=$((FAIL + 1))
fi

echo "=== Test: POST /load with bad file ==="
RESULT=$(curl -s -X POST "${URL}/load" \
    -H 'Content-Type: application/json' \
    -d '{"path":"/tmp/nonexistent_driftnars_file.dnar"}')
check "load bad file" "Failed to load state" "$RESULT"

# Clean up save file
rm -f "$SAVE_PATH"

echo ""
echo "==============================="
echo "  Results: $PASS passed, $FAIL failed"
echo "==============================="
exit "$FAIL"
