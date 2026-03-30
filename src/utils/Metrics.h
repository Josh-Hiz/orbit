#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace orbit {

// Latency histogram
// Records samples in microseconds; buckets are power-of-2 boundaries up to 1 s.
class LatencyHistogram {
  public:
    // Bucket upper bounds in microseconds: 1, 2, 4, 8, … 524288, +inf
    static constexpr int kBuckets = 21;

    void record(std::chrono::nanoseconds ns) noexcept {
        auto raw = ns.count();
        auto us = static_cast<uint64_t>(raw > 0 ? raw / 1000 : 0);
        total_.fetch_add(1, std::memory_order_relaxed);
        sum_us_.fetch_add(us, std::memory_order_relaxed);

        int b = 0;
        uint64_t bound = 1;
        while (b < kBuckets - 1 && us > bound) {
            bound <<= 1;
            ++b;
        }
        buckets_[b].fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t count() const noexcept {
        return total_.load(std::memory_order_relaxed);
    }
    double mean_us() const noexcept {
        auto c = total_.load(std::memory_order_relaxed);
        return c ? static_cast<double>(
                       sum_us_.load(std::memory_order_relaxed)) /
                       c
                 : 0.0;
    }

    // Approximate percentile (linear interp within bucket).
    double percentile_us(double p) const noexcept {
        auto n = total_.load(std::memory_order_relaxed);
        if (!n)
            return 0.0;
        uint64_t target = static_cast<uint64_t>(p / 100.0 * n);
        uint64_t cumul = 0;
        for (int b = 0; b < kBuckets; ++b) {
            cumul += buckets_[b].load(std::memory_order_relaxed);
            if (cumul >= target) {
                uint64_t lo = (b == 0) ? 0 : (1ULL << (b - 1));
                uint64_t hi = (1ULL << b);
                return static_cast<double>(b < kBuckets - 1 ? (lo + hi) / 2
                                                            : hi);
            }
        }
        return static_cast<double>(1ULL << (kBuckets - 1));
    }

    void reset() noexcept {
        total_.store(0, std::memory_order_relaxed);
        sum_us_.store(0, std::memory_order_relaxed);
        for (auto &b : buckets_)
            b.store(0, std::memory_order_relaxed);
    }

  private:
    std::array<std::atomic<uint64_t>, kBuckets> buckets_{};
    std::atomic<uint64_t> total_{0};
    std::atomic<uint64_t> sum_us_{0};
};

// Per-exchange message rate counter
class MsgRateCounter {
  public:
    void tick() noexcept { count_.fetch_add(1, std::memory_order_relaxed); }

    // Returns msgs/sec since last call and resets the counter.
    double rateAndReset() noexcept {
        auto now = std::chrono::steady_clock::now();
        auto prev = last_.exchange(now.time_since_epoch().count(),
                                   std::memory_order_relaxed);
        auto prev_tp = std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(prev));
        double elapsed = std::chrono::duration<double>(now - prev_tp).count();
        auto cnt = count_.exchange(0, std::memory_order_relaxed);
        return elapsed > 0.0 ? cnt / elapsed : 0.0;
    }

  private:
    std::atomic<uint64_t> count_{0};
    std::atomic<int64_t> last_{
        std::chrono::steady_clock::now().time_since_epoch().count()};
};

// Central metrics registry
class Metrics {
  public:
    static Metrics &instance() {
        static Metrics inst;
        return inst;
    }

    // Call when a raw WS message arrives.
    void onMessage(const std::string &exchange) {
        std::lock_guard<std::mutex> lk(mu_);
        rates_[exchange].tick();
        msgTotalByExchange_[exchange].fetch_add(1, std::memory_order_relaxed);
    }

    // Record end-to-end detection latency.
    void recordLatency(std::chrono::nanoseconds ns) { latency_.record(ns); }

    // Increment opportunity counter.
    void onOpportunity() {
        opportunityCount_.fetch_add(1, std::memory_order_relaxed);
    }

    uint64_t opportunityCount() const noexcept {
        return opportunityCount_.load(std::memory_order_relaxed);
    }

    LatencyHistogram &latency() noexcept { return latency_; }

    // Print a summary to stderr.
    void printSummary(const std::string &exchange) {
        std::lock_guard<std::mutex> lk(mu_);
        double rate = rates_[exchange].rateAndReset();
        auto total =
            msgTotalByExchange_[exchange].load(std::memory_order_relaxed);
        std::fprintf(stderr,
                     "[Metrics] %-12s  rate=%.1f msg/s  total=%llu  opps=%llu"
                     "  lat_mean=%.1f us  p50=%.1f us  p99=%.1f us\n",
                     exchange.c_str(), rate,
                     static_cast<unsigned long long>(total),
                     static_cast<unsigned long long>(opportunityCount_.load()),
                     latency_.mean_us(), latency_.percentile_us(50.0),
                     latency_.percentile_us(99.0));
    }

  private:
    Metrics() = default;

    std::mutex mu_;
    std::unordered_map<std::string, MsgRateCounter> rates_;
    std::unordered_map<std::string, std::atomic<uint64_t>> msgTotalByExchange_;
    LatencyHistogram latency_;
    std::atomic<uint64_t> opportunityCount_{0};
};

// RAII latency scope timer
struct ScopedTimer {
    explicit ScopedTimer(Metrics &m)
        : m_(m), t0_(std::chrono::steady_clock::now()) {}
    ~ScopedTimer() {
        auto ns = std::chrono::steady_clock::now() - t0_;
        m_.recordLatency(ns);
    }

  private:
    Metrics &m_;
    std::chrono::steady_clock::time_point t0_;
};

} // namespace orbit