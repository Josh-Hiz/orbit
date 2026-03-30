#include "ArbitrageEngine.h"
#include "../utils/Logger.h"
#include "../utils/Metrics.h"

#include <chrono>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace orbit {

static constexpr const char *TAG = "Arbitrage";

// Construction
ArbitrageEngine::ArbitrageEngine(PriceTable &table, const Config &cfg)
    : table_(table), cfg_(cfg), minSpreadPct_(cfg.minSpreadPct),
      staleMs_(cfg.staleMs) {}

ArbitrageEngine::~ArbitrageEngine() { stop(); }

// Lifecycle
void ArbitrageEngine::start() {
    if (running_.exchange(true))
        return; // already running
    thread_ = std::thread(&ArbitrageEngine::scanLoop, this);
    LOG_INFO(TAG, "ArbitrageEngine started (minSpread=", minSpreadPct_.load(),
             "% stale=", staleMs_.load(), "ms)");
}

void ArbitrageEngine::stop() {
    if (!running_.exchange(false))
        return;
    // Unblock waitForUpdate if the engine is sleeping.
    table_.waitForUpdate(std::chrono::milliseconds(0));
    if (thread_.joinable())
        thread_.join();
    LOG_INFO(TAG, "ArbitrageEngine stopped. Total opportunities: ",
             totalOpps_.load());
}

// Configuration
void ArbitrageEngine::setCallback(OpportunityCallback cb) {
    std::lock_guard<std::mutex> lk(cbMu_);
    callback_ = std::move(cb);
}

void ArbitrageEngine::setMinSpreadPct(double pct) noexcept {
    minSpreadPct_.store(pct, std::memory_order_relaxed);
}

void ArbitrageEngine::setStaleMs(long ms) noexcept {
    staleMs_.store(ms, std::memory_order_relaxed);
}

uint64_t ArbitrageEngine::totalOpportunities() const noexcept {
    return totalOpps_.load(std::memory_order_relaxed);
}

// Main loop
void ArbitrageEngine::scanLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        // Block until a price update arrives (or timeout).
        table_.waitForUpdate(std::chrono::milliseconds(100));
        if (!running_.load(std::memory_order_relaxed))
            break;
        scan();
    }
}

// Core scan
void ArbitrageEngine::scan() {
    ScopedTimer timer(Metrics::instance());

    const double minSpread = minSpreadPct_.load(std::memory_order_relaxed);

    for (const auto &symbol : cfg_.symbols) {
        auto quotes = table_.getAll(symbol); // snapshot

        // Collect valid, fresh quotes into a flat list.
        struct Entry {
            std::string ex;
            Quote q;
        };
        std::vector<Entry> valid;
        valid.reserve(quotes.size());
        for (auto &[ex, q] : quotes) {
            if (!q.valid)
                continue;
            if (isStale(q))
                continue;
            valid.push_back({ex, q});
        }

        if (valid.size() < 2)
            continue;

        // Check all ordered pairs (A, B): buy on A at ask, sell on B at bid.
        for (std::size_t i = 0; i < valid.size(); ++i) {
            for (std::size_t j = 0; j < valid.size(); ++j) {
                if (i == j)
                    continue;

                const auto &buyer = valid[i];  // we buy at buyer.q.ask
                const auto &seller = valid[j]; // we sell at seller.q.bid

                if (buyer.q.ask <= 0.0 || seller.q.bid <= 0.0)
                    continue;

                double spread =
                    (seller.q.bid - buyer.q.ask) / buyer.q.ask * 100.0;

                if (spread >= minSpread) {
                    ArbitrageOpportunity opp{symbol,
                                             buyer.ex,
                                             seller.ex,
                                             buyer.q.ask,
                                             seller.q.bid,
                                             spread,
                                             std::chrono::steady_clock::now()};
                    report(opp);
                }
            }
        }
    }
}

// Staleness check
bool ArbitrageEngine::isStale(const Quote &q) const noexcept {
    auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();
    auto ageMs = (nowUs - q.timestampUs) / 1000LL;
    return ageMs > staleMs_.load(std::memory_order_relaxed);
}

// Report
void ArbitrageEngine::report(const ArbitrageOpportunity &opp) {
    auto now = std::chrono::steady_clock::now();

    // Suppress repeated alerts for the same direction within kSuppressMs.
    SuppressKey key{opp.symbol, opp.buyExchange, opp.sellExchange};
    auto it = lastSeen_.find(key);
    if (it != lastSeen_.end()) {
        auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                         now - it->second)
                         .count();
        if (ageMs < kSuppressMs)
            return;
    }
    lastSeen_[key] = now;

    totalOpps_.fetch_add(1, std::memory_order_relaxed);
    Metrics::instance().onOpportunity();

    // Format and print the alert.
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6);
    oss << "OPPORTUNITY  " << opp.symbol << "  BUY " << opp.buyExchange << "@"
        << opp.buyPrice << "  SELL " << opp.sellExchange << "@" << opp.sellPrice
        << "  spread=" << std::setprecision(4) << opp.spreadPct << "%";

    LOG_INFO(TAG, oss.str());

    // Fire user callback.
    std::lock_guard<std::mutex> lk(cbMu_);
    if (callback_)
        callback_(opp);
}

} // namespace orbit