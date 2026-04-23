#include "ringbuf/v04/ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

using ringbuf::v04::RingBuffer;

// N은 2의 거듭제곱이어야 함. 버퍼 크기는 N = chunk * 2.

// --- write + read 사이클 (wrap 없음) ---
template <std::size_t N>
static void BM_WriteRead(benchmark::State& state) {
    constexpr std::size_t chunk = N / 2;
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);

    RingBuffer<N> rb;

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK_TEMPLATE(BM_WriteRead, 32);
BENCHMARK_TEMPLATE(BM_WriteRead, 128);
BENCHMARK_TEMPLATE(BM_WriteRead, 512);
BENCHMARK_TEMPLATE(BM_WriteRead, 2048);
BENCHMARK_TEMPLATE(BM_WriteRead, 8192);
BENCHMARK_TEMPLATE(BM_WriteRead, 16384);
BENCHMARK_TEMPLATE(BM_WriteRead, 32768);
BENCHMARK_TEMPLATE(BM_WriteRead, 65536);
BENCHMARK_TEMPLATE(BM_WriteRead, 131072);

// --- zero-copy (wrap 없음) ---
template <std::size_t N>
static void BM_ZeroCopy(benchmark::State& state) {
    constexpr std::size_t chunk = N / 2;
    std::vector<std::byte> src(chunk, std::byte{0xAB});

    RingBuffer<N> rb;

    for (auto _ : state) {
        std::memcpy(rb.write_ptr(), src.data(), chunk);
        rb.move_write_pos(chunk);
        benchmark::DoNotOptimize(rb.read_ptr());
        rb.move_read_pos(chunk);
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk)
    );
}
BENCHMARK_TEMPLATE(BM_ZeroCopy, 32);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 128);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 512);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 2048);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 8192);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 16384);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 32768);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 65536);
BENCHMARK_TEMPLATE(BM_ZeroCopy, 131072);

BENCHMARK_MAIN();
