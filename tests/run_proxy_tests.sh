#!/usr/bin/env bash
# End-to-end smoke tests for TinyProxy.
#
# These tests start local origin servers, send requests through the proxy,
# and check the behavior a backend interviewer is likely to ask about:
# forwarding, binary response handling, concurrency, and cache hits.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
PROXY_PID=""
ORIGIN_PID=""
CACHE_ORIGIN_PID=""

cleanup() {
    if [[ -n "${PROXY_PID}" ]]; then
        kill "${PROXY_PID}" 2>/dev/null || true
    fi
    if [[ -n "${ORIGIN_PID}" ]]; then
        kill "${ORIGIN_PID}" 2>/dev/null || true
    fi
    if [[ -n "${CACHE_ORIGIN_PID}" ]]; then
        kill "${CACHE_ORIGIN_PID}" 2>/dev/null || true
    fi
    rm -rf "${TMP_DIR}"
}
trap cleanup EXIT

free_port() {
    python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

wait_for_port() {
    local port="$1"
    python3 - "${port}" <<'PY'
import socket
import sys
import time

port = int(sys.argv[1])
deadline = time.time() + 5
while time.time() < deadline:
    try:
        with socket.create_connection(("127.0.0.1", port), timeout=0.2):
            sys.exit(0)
    except OSError:
        time.sleep(0.05)

sys.exit(1)
PY
}

assert_equal_files() {
    local expected="$1"
    local actual="$2"
    local label="$3"

    if ! cmp -s "${expected}" "${actual}"; then
        echo "FAIL: ${label}"
        exit 1
    fi
}

echo "Building proxy..."
make -C "${ROOT_DIR}" >/dev/null

origin_port="$(free_port)"
proxy_port="$(free_port)"
cache_origin_port="$(free_port)"

mkdir -p "${TMP_DIR}/origin" "${TMP_DIR}/downloads"
printf "hello through proxy\n" > "${TMP_DIR}/origin/index.txt"
python3 - <<PY
from pathlib import Path
Path("${TMP_DIR}/origin/blob.bin").write_bytes(bytes(range(256)) * 8)
PY

python3 -m http.server "${origin_port}" --bind 127.0.0.1 --directory "${TMP_DIR}/origin" >/dev/null 2>&1 &
ORIGIN_PID="$!"
wait_for_port "${origin_port}"

"${ROOT_DIR}/proxy" "${proxy_port}" >/dev/null 2>&1 &
PROXY_PID="$!"
wait_for_port "${proxy_port}"

echo "Testing basic text forwarding..."
curl --silent --fail --proxy "http://127.0.0.1:${proxy_port}" \
    "http://127.0.0.1:${origin_port}/index.txt" \
    --output "${TMP_DIR}/downloads/index.txt"
assert_equal_files "${TMP_DIR}/origin/index.txt" "${TMP_DIR}/downloads/index.txt" "text response changed"

echo "Testing binary forwarding..."
curl --silent --fail --proxy "http://127.0.0.1:${proxy_port}" \
    "http://127.0.0.1:${origin_port}/blob.bin" \
    --output "${TMP_DIR}/downloads/blob.bin"
assert_equal_files "${TMP_DIR}/origin/blob.bin" "${TMP_DIR}/downloads/blob.bin" "binary response changed"

echo "Testing concurrent requests..."
for i in $(seq 1 20); do
    curl --silent --fail --proxy "http://127.0.0.1:${proxy_port}" \
        "http://127.0.0.1:${origin_port}/index.txt?request=${i}" \
        --output "${TMP_DIR}/downloads/concurrent-${i}.txt" &
done
wait

for i in $(seq 1 20); do
    assert_equal_files "${TMP_DIR}/origin/index.txt" "${TMP_DIR}/downloads/concurrent-${i}.txt" "concurrent response ${i} changed"
done

echo "Testing cache hit behavior..."
counter_file="${TMP_DIR}/origin-hit-count.txt"
python3 "${ROOT_DIR}/tests/cache_origin.py" \
    --port "${cache_origin_port}" \
    --counter-file "${counter_file}" >/dev/null 2>&1 &
CACHE_ORIGIN_PID="$!"
wait_for_port "${cache_origin_port}"

curl --silent --fail --proxy "http://127.0.0.1:${proxy_port}" \
    "http://127.0.0.1:${cache_origin_port}/cache-me.txt" \
    --output "${TMP_DIR}/downloads/cache-first.txt"
curl --silent --fail --proxy "http://127.0.0.1:${proxy_port}" \
    "http://127.0.0.1:${cache_origin_port}/cache-me.txt" \
    --output "${TMP_DIR}/downloads/cache-second.txt"

assert_equal_files "${TMP_DIR}/downloads/cache-first.txt" "${TMP_DIR}/downloads/cache-second.txt" "cached response changed"

origin_hits="$(cat "${counter_file}")"
if [[ "${origin_hits}" != "1" ]]; then
    echo "FAIL: expected 1 origin hit after repeated cached request, got ${origin_hits}"
    exit 1
fi

echo "PASS: forwarding, binary responses, concurrency, and cache hits all look good."
