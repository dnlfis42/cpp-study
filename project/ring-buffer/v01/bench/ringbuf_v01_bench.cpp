#include "ringbuf/v01/ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

using ringbuf::v01::RingBuffer;

// --- write + read 사이클 (wrap 없음) ---
static void BM_WriteRead(benchmark::State& state) {
    const std::size_t chunk = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);

    RingBuffer rb{chunk * 2};

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK(BM_WriteRead)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(8192)
    ->Arg(16384)
    ->Arg(32768)
    ->Arg(65536);

// --- wrap 강제 ---
static void BM_WriteReadWrap(benchmark::State& state) {
    const std::size_t chunk = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);

    RingBuffer rb{chunk + chunk / 2};

    rb.write(src.data(), chunk / 2);
    rb.read(dst.data(), chunk / 2);

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK(BM_WriteReadWrap)
    ->Arg(16)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(8192)
    ->Arg(16384)
    ->Arg(32768)
    ->Arg(65536);

// --- zero-copy (wrap 없음) ---
// write_ptr + memcpy + move_write_pos (단방향 memcpy 비용 측정)
static void BM_ZeroCopy(benchmark::State& state) {
    const std::size_t chunk = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(chunk, std::byte{0xAB});

    RingBuffer rb{chunk * 2};

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
BENCHMARK(BM_ZeroCopy)
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
