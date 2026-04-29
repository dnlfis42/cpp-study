#include "linbuf/v01/linear_buffer.hpp"
#include "linbuf/v02/linear_buffer.hpp"

#include <benchmark/benchmark.h>

#include <vector>

#include <cstddef>
#include <cstdint>
#include <cstring>

static constexpr std::size_t n = 65536;

// --- v01 ---

static void BM_v01_RawWrite(benchmark::State& state) {
    std::vector<std::byte> src(n, std::byte{0xAB});
    linbuf::v01::LinearBuffer lb{n * 2};

    lb.write(src.data(), n);
    lb.clear();

    for (auto _ : state) {
        lb.write(src.data(), n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_v01_RawWrite);

static void BM_v01_ZeroCopyWrite(benchmark::State& state) {
    std::vector<std::byte> src(n, std::byte{0xAB});
    linbuf::v01::LinearBuffer lb{n * 2};

    std::memcpy(lb.write_ptr(), src.data(), n);
    lb.move_write_pos(n);
    lb.clear();

    for (auto _ : state) {
        std::memcpy(lb.write_ptr(), src.data(), n);
        lb.move_write_pos(n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_v01_ZeroCopyWrite);

// --- v02 ---

static void BM_v02_RawWrite(benchmark::State& state) {
    std::vector<std::byte> src(n, std::byte{0xAB});
    linbuf::v02::LinearBuffer lb{n * 2};

    lb.write(src.data(), n);
    lb.clear();

    for (auto _ : state) {
        lb.write(src.data(), n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_v02_RawWrite);

static void BM_v02_ZeroCopyWrite(benchmark::State& state) {
    std::vector<std::byte> src(n, std::byte{0xAB});
    linbuf::v02::LinearBuffer lb{n * 2};

    std::memcpy(lb.write_ptr(), src.data(), n);
    lb.move_write_pos(n);
    lb.clear();

    for (auto _ : state) {
        std::memcpy(lb.write_ptr(), src.data(), n);
        lb.move_write_pos(n);
        lb.clear();
        benchmark::DoNotOptimize(lb);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_v02_ZeroCopyWrite);

BENCHMARK_MAIN();
