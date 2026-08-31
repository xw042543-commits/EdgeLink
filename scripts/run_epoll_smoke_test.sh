#!/usr/bin/env bash

set -euo pipefail

port="${1:-19040}"
clients="${2:-20}"
messages_per_client="${3:-20}"
gateway_log="$(mktemp)"
gateway_pid=""

cleanup() {
    if [[ -n "${gateway_pid}" ]] && kill -0 "${gateway_pid}" 2>/dev/null; then
        kill -INT "${gateway_pid}" 2>/dev/null || true
        wait "${gateway_pid}" 2>/dev/null || true
    fi
    rm -f "${gateway_log}"
}
trap cleanup EXIT

./build/epoll_gateway "${port}" >"${gateway_log}" 2>&1 &
gateway_pid="$!"

ready=false
for _ in {1..50}; do
    if (exec 3<>"/dev/tcp/127.0.0.1/${port}") 2>/dev/null; then
        exec 3>&-
        ready=true
        break
    fi
    sleep 0.1
done

if [[ "${ready}" != true ]]; then
    echo "epoll gateway did not start" >&2
    cat "${gateway_log}" >&2
    exit 1
fi

./build/load_generator \
    127.0.0.1 \
    "${port}" \
    "${clients}" \
    "${messages_per_client}"

kill -INT "${gateway_pid}"
wait "${gateway_pid}"
gateway_pid=""

if ! grep -q "epoll gateway stopped" "${gateway_log}"; then
    echo "epoll gateway did not stop cleanly" >&2
    cat "${gateway_log}" >&2
    exit 1
fi

echo "PASS: epoll smoke test"
