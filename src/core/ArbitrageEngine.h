#pragma once

#include "../utils/Config.h"
#include "PriceTable.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace orbit {

// Opportunity record
struct ArbitrageOpportunity {
    std::string symbol;
    std::string buyExchange;  // buy here (at ask)
    std::string sellExchange; // sell here (at bid)
    double buyPrice{0.0};
    double sellPrice{0.0};
    double spreadPct{0.0};
    std::chrono::steady_clock::time_point detectedAt;
};

using OpportunityCallback = std::function<void(const ArbitrageOpportunity &)>;

// Engine
class ArbitrageEngine {
  public:
    explicit ArbitrageEngine(PriceTable &table, const Config &cfg);
    ~ArbitrageEngine();

    // Start the background scanning thread.
    void start();

    // Stop and join the scanning thread.
    void stop();

    // Register a callback invoked for every detected opportunity.
    void setCallback(OpportunityCallback cb);

    // Runtime configuration updates (thread-safe).
    void setMinSpreadPct(double pct) noexcept;
    void setStaleMs(long ms) noexcept;

    bool isRunning() const noexcept {
        return running_.load(std::memory_order_relaxed);
    }

    uint64_t totalOpportunities() const noexcept;

  private:
    void scanLoop();
    void scan();
    bool isStale(const Quote &q) const noexcept;
    void report(const ArbitrageOpportunity &opp);

    PriceTable &table_;
    Config cfg_; // local copy (engine modifies nothing shared)

    std::atomic<double> minSpreadPct_;
    std::atomic<long> staleMs_;

    OpportunityCallback callback_;
    std::mutex cbMu_;

    std::atomic<bool> running_{false};
    std::thread thread_;

    std::atomic<uint64_t> totalOpps_{0};

    // Track last reported opportunity per (symbol,buy,sell) to avoid spam.
    struct SuppressKey {
        std::string symbol, buy, sell;
        bool operator==(const SuppressKey &o) const {
            return symbol == o.symbol && buy == o.buy && sell == o.sell;
        }
    };
    struct SuppressHash {
        std::size_t operator()(const SuppressKey &k) const {
            auto h = std::hash<std::string>{};
            return h(k.symbol) ^ (h(k.buy) << 1) ^ (h(k.sell) << 2);
        }
    };
    std::unordered_map<SuppressKey, std::chrono::steady_clock::time_point,
                       SuppressHash>
        lastSeen_;
    static constexpr long kSuppressMs = 500; // min ms between repeated alerts
};

} // namespace orbit