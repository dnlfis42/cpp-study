#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="$ROOT/build/release/bin/cache_align_false_sharing_bench"
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

# 코어 핀은 bench 내부에서 (코어 2, 4 사용). taskset 안 함.
"$BENCH"
