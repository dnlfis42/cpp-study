#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BENCH="$ROOT/build/release/bin/bench_objpool_v05"
RUNS=${RUNS:-1}
COOLDOWN=${COOLDOWN:-10}
FREQ_LIMIT=${FREQ_LIMIT:-3.0GHz}

cleanup() {
    echo
    echo "[cleanup] 주파수 상한 해제 (4.5GHz)"
    sudo cpupower frequency-set -u 4.5GHz > /dev/null || true
}
trap cleanup EXIT

echo "[setup] governor: performance, 상한 $FREQ_LIMIT"
sudo cpupower frequency-set -g performance > /dev/null
sudo cpupower frequency-set -u "$FREQ_LIMIT" > /dev/null

for i in $(seq 1 "$RUNS"); do
    echo
    echo "=== run $i / $RUNS ==="
    taskset -c 2 "$BENCH" \
        --benchmark_repetitions=10 \
        --benchmark_report_aggregates_only=true \
        --benchmark_display_aggregates_only=true

    if [ "$i" -lt "$RUNS" ]; then
        echo
        echo "[cooldown] ${COOLDOWN}s..."
        sleep "$COOLDOWN"
    fi
done
