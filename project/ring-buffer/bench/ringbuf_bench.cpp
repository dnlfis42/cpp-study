#include "ringbuf/v01/ring_buffer.hpp"
#include "ringbuf/v02/ring_buffer.hpp"
#include "ringbuf/v03/ring_buffer.hpp"
#include "ringbuf/v04/ring_buffer.hpp"

#include <benchmark/benchmark.h>

#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

// WriteRead: write(chunk) + read(chunk), 2x memcpy, bandwidth = chunk * 2.
// Key sizes: N=128 (chunk=64, SIMD inline), N=8192 (chunk=4096, rep movsq
// boundary), N=131072 (chunk=65536, bandwidth).

static void BM_v01_WriteRead(benchmark::State& state) {
    const std::size_t chunk = static_cast<std::size_t>(state.range(0));
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);
    ringbuf::v01::RingBuffer rb{chunk * 2};

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK(BM_v01_WriteRead)->Arg(64)->Arg(4096)->Arg(65536);

template <std::size_t N>
static void BM_v02_WriteRead(benchmark::State& state) {
    constexpr std::size_t chunk = N / 2;
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);
    ringbuf::v02::RingBuffer<N> rb;

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK_TEMPLATE(BM_v02_WriteRead, 128);
BENCHMARK_TEMPLATE(BM_v02_WriteRead, 8192);
BENCHMARK_TEMPLATE(BM_v02_WriteRead, 131072);

template <std::size_t N>
static void BM_v03_WriteRead(benchmark::State& state) {
    constexpr std::size_t chunk = N / 2;
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);
    ringbuf::v03::RingBuffer<N> rb;

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK_TEMPLATE(BM_v03_WriteRead, 128);
BENCHMARK_TEMPLATE(BM_v03_WriteRead, 8192);
BENCHMARK_TEMPLATE(BM_v03_WriteRead, 131072);

template <std::size_t N>
static void BM_v04_WriteRead(benchmark::State& state) {
    constexpr std::size_t chunk = N / 2;
    std::vector<std::byte> src(chunk, std::byte{0xAB});
    std::vector<std::byte> dst(chunk);
    ringbuf::v04::RingBuffer<N> rb;

    for (auto _ : state) {
        benchmark::DoNotOptimize(rb.write(src.data(), chunk));
        benchmark::DoNotOptimize(rb.read(dst.data(), chunk));
    }

    state.SetBytesProcessed(
        state.iterations() * static_cast<std::int64_t>(chunk) * 2
    );
}
BENCHMARK_TEMPLATE(BM_v04_WriteRead, 128);
BENCHMARK_TEMPLATE(BM_v04_WriteRead, 8192);
BENCHMARK_TEMPLATE(BM_v04_WriteRead, 131072);

BENCHMARK_MAIN();
