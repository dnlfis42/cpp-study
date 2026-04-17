#include "linbuf/v02/linear_buffer.hpp"

#include <benchmark/benchmark.h>

#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

using linbuf::v02::LinearBuffer;

// --- raw write (사이즈별) ---
static void BM_RawWrite(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(n, std::byte{0xAB});

    LinearBuffer lb{n * 2};

    for (auto _ : state) {
        lb.write(src.data(), n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_RawWrite)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(8192)
    ->Arg(16384)
    ->Arg(32768)
    ->Arg(65536);

// --- zero-copy recv 시뮬레이션 ---
static void BM_ZeroCopyWrite(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(n, std::byte{0xAB});

    LinearBuffer lb{n * 2};

    for (auto _ : state) {
        std::memcpy(lb.write_ptr(), src.data(), n);
        lb.move_write_pos(n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_ZeroCopyWrite)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(8192)
    ->Arg(16384)
    ->Arg(32768)
    ->Arg(65536);

// --- v02 신규: read(n) zero-copy read ---
// write + read_span을 한 iteration에 같이 측정.
// 순수 read_span 비용은 BM_RawWrite와 비교해서 추정.
static void BM_ReadSpan(benchmark::State& state) {
    const std::size_t n = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(n, std::byte{0xAB});

    LinearBuffer lb{n * 2};

    for (auto _ : state) {
        lb.write(src.data(), n);
        auto span = lb.read(n);
        benchmark::DoNotOptimize(span.data());
        benchmark::DoNotOptimize(span.size());
        lb.clear();
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_ReadSpan)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(8192)
    ->Arg(16384)
    ->Arg(32768)
    ->Arg(65536);

BENCHMARK_MAIN();
