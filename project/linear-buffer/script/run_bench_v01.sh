#!/bin/bash
set -e

# 안정 측정 전략
# - 주파수를 3 GHz로 상한 고정 → boost/throttle 사이클 제거
# - CPU 2번 코어에 고정
# - 반복 실행 사이 쿨다운

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
BENCH="$ROOT/build/release/bin/bench_linbuf_v01"
RUNS=${RUNS:-3}
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
