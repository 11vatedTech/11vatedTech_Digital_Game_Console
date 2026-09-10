// bench_cpu.cpp — CPU qualification (DK0-M1A; docs/benchmarks.md).
// Workloads are representative game-code shapes, not vendor-style synthetic
// peak claims: serial simulation, scene traversal, entity update, command prep.
#include "bench_common.hpp"
#include "dc/qualification.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace dcwin {

namespace {

// Deterministic input mixture: traversals, entity updates, command prep, math.
struct GameWork {
    uint64_t seed = 0;
    uint64_t Run(uint64_t iters) const {
        BenchRng rng(seed);
        uint64_t acc = 0;
        // Simulated entity block: 4096 entities, mixed int/FP ops.
        double x = 1.0, y = 0.5, z = 0.25;
        for (uint64_t i = 0; i < iters; ++i) {
            // scene traversal (pointer-chase-like dependent adds)
            uint64_t h = rng.Next();
            acc ^= h >> 33;
            // entity update (FP math chain)
            x = x * 1.0000001 + y * 0.5 + z * 0.25;
            y = y * 0.9999999 + z * 0.125 + x * 0.5;
            z = z + x * 0.03125 - y * 0.015625;
            // command prep (branchy integer work)
            if (h & 1) acc += i & 0xFF;
            else acc -= (h >> 7) & 0x3F;
            if ((i & 0xFFF) == 0) { x = std::sqrt(x) + 1e-9; }
        }
        return acc ^ (static_cast<uint64_t>(x * 4096.0));
    }
};

// Worker job: identical shape to game work but independent input.
struct WorkerJob {
    uint64_t seed = 0;
    uint64_t iters = 0;
    uint64_t sink = 0;
    void Run() { sink = GameWork{seed}.Run(iters); }
};

constexpr uint64_t kItersPerSample = 600000;   // per fixed-work sample
constexpr uint64_t kWarmupIters = 200000;

// File-scope volatile sink: forces every timed workload's result to exist at
// the measurement point (prevents cross-QPC float of pure workloads).
static volatile uint64_t s_sink = 0;

} // namespace

void BenchCpuSingleThread(const BenchIdentity& id, BenchCancel& cancel,
                          BenchmarkRecord& rec) {
    StampRecord(rec, id, "cpu.single-thread", static_cast<uint32_t>(kItersPerSample), 1,
                "dc-bench/1:cpu-single");
    GameWork w{0xDC00C0FFEEULL};
    w.Run(kWarmupIters); // warmup

    // Volatile sink: prevents the optimizer from eliminating the workload
    // (first run measured dt=0 because the unused result was folded away).

    std::vector<BenchSample> samples;
    for (int s = 0; s < 5; ++s) {
        if (cancel.requested || cancel.Expired(QpcMs())) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
        double t0 = QpcMs();
        uint64_t sink = w.Run(kItersPerSample);
        DoNotOptimize(sink); // keep the workload inside the timed region
        double dt = QpcMs() - t0;
        s_sink = sink;
        samples.push_back({dt, dt});
    }
    FinalizeRecord(rec, samples, static_cast<double>(kItersPerSample));
}

void BenchCpuGameThread(const BenchIdentity& id, BenchCancel& cancel,
                        BenchmarkRecord& rec) {
    StampRecord(rec, id, "cpu.game-thread", static_cast<uint32_t>(kItersPerSample), 1,
                "dc-bench/1:cpu-gamethread");
    // Game thread on a dedicated thread at normal priority; main thread waits.
    std::atomic<uint64_t> sink{0};
    std::atomic<bool> done{false};
    std::vector<BenchSample> samples;
    std::thread worker([&] {
        GameWork w{0xDCAFE12345678ULL};
        w.Run(kWarmupIters);
        for (int s = 0; s < 5; ++s) {
            if (cancel.requested) { done = true; return; }
            double t0 = QpcMs();
            uint64_t r = w.Run(kItersPerSample);
            double dt = QpcMs() - t0;
            samples.push_back({dt, dt});
            sink = r;
        }
        done = true;
    });
    SetThreadPriority(worker.native_handle(), THREAD_PRIORITY_NORMAL);
    while (!done) std::this_thread::yield();
    worker.join();
    if (cancel.requested) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
    FinalizeRecord(rec, samples, static_cast<double>(kItersPerSample));
}

void BenchCpuWorker(const BenchIdentity& id, BenchCancel& cancel,
                    BenchmarkRecord& rec, uint32_t logical_cores) {
    StampRecord(rec, id, "cpu.worker", static_cast<uint32_t>(kItersPerSample), 1,
                "dc-bench/1:cpu-worker");
    // Reserve 2 cores (main + system): realistic game worker budget.
    uint32_t workers = logical_cores > 2 ? logical_cores - 2 : 1;
    const uint64_t per_worker = kItersPerSample / workers;
    std::vector<BenchSample> samples;
    for (int s = 0; s < 5; ++s) {
        if (cancel.requested || cancel.Expired(QpcMs())) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
        std::vector<WorkerJob> jobs(workers);
        for (uint32_t j = 0; j < workers; ++j) { jobs[j].seed = 0x5EED0000ULL + j; jobs[j].iters = per_worker; }
        double t0 = QpcMs();
        {
            std::vector<std::thread> threads;
            threads.reserve(workers);
            for (uint32_t j = 0; j < workers; ++j) threads.emplace_back([&jobs, j] { jobs[j].Run(); });
            for (auto& t : threads) t.join();
        }
        double dt = QpcMs() - t0;
        uint64_t sink = 0;
        for (auto& j : jobs) sink ^= j.sink;
        (void)sink;
        // iterations are total game-work units across workers
        samples.push_back({dt, dt});
    }
    FinalizeRecord(rec, samples, static_cast<double>(kItersPerSample));
}

void BenchCpuSustained(const BenchIdentity& id, BenchCancel& cancel,
                       BenchmarkRecord& rec, const SustainedConfig& cfg) {
    StampRecord(rec, id, "cpu.game-thread.sustained", static_cast<uint32_t>(kItersPerSample), 1,
                "dc-bench/1:cpu-gamethread");
    GameWork w{0xDCAFE12345678ULL};
    // Sustained methodology (fixed 2026-09-10, DevKit-0): the previous design
    // YIELDED between samples, so the core dropped toward idle and every
    // sample restarted on a ramping clock — a monotonic 10.3→3.4 ms ramp that
    // honestly tripped "unstable-samples". A sustained benchmark must keep
    // the core under CONTINUOUS load: a time-based steady-state warmup first,
    // then untimed filler work between timed samples so the load never drops.
    const double warmup_window_ms =
        cfg.window_ms * 0.25 < 5000.0 ? cfg.window_ms * 0.25 : 5000.0;
    double warmup_start = QpcMs();
    while (QpcMs() - warmup_start < warmup_window_ms) {
        if (cancel.requested) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
        s_sink = w.Run(kItersPerSample); // untimed warmup/filler work
    }
    std::vector<BenchSample> samples;
    double window_start = QpcMs();
    // Distribute cfg.samples across the sustained window; each sample is one
    // fixed-work iteration; filler work runs between samples so the core
    // stays under sustained load for the whole window.
    double window_per_sample = cfg.window_ms / static_cast<double>(cfg.samples);
    for (uint32_t s = 0; s < cfg.samples; ++s) {
        if (cancel.Expired(QpcMs())) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
        double t0 = QpcMs();
        uint64_t sink = w.Run(kItersPerSample);
        DoNotOptimize(sink);
        double dt = QpcMs() - t0;
        s_sink = sink; // volatile store: the value must exist NOW
        samples.push_back({dt, dt});
        double target = window_start + window_per_sample * (s + 1);
        while (QpcMs() < target) {
            if (cancel.requested) { rec.state = BenchState::Aborted; rec.abort_reason = "cancelled"; return; }
            s_sink = w.Run(kItersPerSample); // filler: sustained load, untimed
        }
    }
    FinalizeRecord(rec, samples, static_cast<double>(kItersPerSample));
}

} // namespace dcwin
