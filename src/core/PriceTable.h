#pragma once

// Thread-safe store of best bid/ask quotes, keyed by (symbol, exchange).
// Writers: one thread per exchange WebSocket client.
// Readers: ArbitrageEngine thread.
//
// Design: shared_mutex for O(1) concurrent reads; separate mutex+CV for
// wake-ups.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace orbit {

// Quote
struct Quote {
    double bid{0.0};    // best bid price
    double ask{0.0};    // best ask price
    double bidQty{0.0}; // best bid quantity (informational)
    double askQty{0.0}; // best ask quantity (informational)
    int64_t timestampUs{
        0}; // exchange-reported or local arrival time (us since epoch)
    bool valid{false};
};

// PriceTable
class PriceTable {
  public:
    using UpdateCb = std::function<void(
        const std::string &symbol, const std::string &exchange, const Quote &)>;

    // Write
    void update(const std::string &symbol, const std::string &exchange,
                double bid, double ask, double bidQty = 0.0,
                double askQty = 0.0, int64_t tsUs = 0);

    // Read
    std::optional<Quote> get(const std::string &symbol,
                             const std::string &exchange) const;

    // Returns all exchange quotes for a given symbol.
    std::unordered_map<std::string, Quote>
    getAll(const std::string &symbol) const;

    // Notification
    // Register a callback fired on every update (called on the writer thread).
    void setUpdateCallback(UpdateCb cb);

    // Block the calling thread until at least one update arrives or timeout.
    bool waitForUpdate(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(200));

    // Diagnostics
    std::size_t symbolCount() const;
    std::size_t exchangeCount(const std::string &symbol) const;
    uint64_t totalUpdates() const noexcept;

  private:
    // symbol -> exchange -> quote
    mutable std::shared_mutex dataMu_;
    std::unordered_map<std::string, std::unordered_map<std::string, Quote>>
        table_;

    // Condition variable wake-up for ArbitrageEngine.
    mutable std::mutex cvMu_;
    std::condition_variable cv_;
    bool updated_{false};

    UpdateCb callback_;
    std::atomic<uint64_t> totalUpdates_{0};
};

} // namespace orbit