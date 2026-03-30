#include "PriceTable.h"
#include <chrono>

namespace orbit {

// Write
void PriceTable::update(const std::string &symbol, const std::string &exchange,
                        double bid, double ask, double bidQty, double askQty,
                        int64_t tsUs) {
    if (bid <= 0.0 || ask <= 0.0 || bid > ask)
        return;

    int64_t arrival = tsUs;
    if (arrival == 0) {
        arrival = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
    }

    Quote q{bid, ask, bidQty, askQty, arrival, true};

    {
        std::unique_lock<std::shared_mutex> lk(dataMu_);
        table_[symbol][exchange] = q;
    }

    totalUpdates_.fetch_add(1, std::memory_order_relaxed);

    // Fire optional callback (on caller thread – keep it fast).
    if (callback_)
        callback_(symbol, exchange, q);

    // Wake up waiters.
    {
        std::lock_guard<std::mutex> lk(cvMu_);
        updated_ = true;
    }
    cv_.notify_one();
}

// Read
std::optional<Quote> PriceTable::get(const std::string &symbol,
                                     const std::string &exchange) const {
    std::shared_lock<std::shared_mutex> lk(dataMu_);
    auto it = table_.find(symbol);
    if (it == table_.end())
        return std::nullopt;
    auto it2 = it->second.find(exchange);
    if (it2 == it->second.end())
        return std::nullopt;
    return it2->second;
}

std::unordered_map<std::string, Quote>
PriceTable::getAll(const std::string &symbol) const {
    std::shared_lock<std::shared_mutex> lk(dataMu_);
    auto it = table_.find(symbol);
    if (it == table_.end())
        return {};
    return it->second; // copy (cheap for 4 exchanges)
}

// Notification
void PriceTable::setUpdateCallback(UpdateCb cb) {
    std::unique_lock<std::shared_mutex> lk(dataMu_);
    callback_ = std::move(cb);
}

bool PriceTable::waitForUpdate(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(cvMu_);
    bool fired = cv_.wait_for(lk, timeout, [this] { return updated_; });
    if (fired)
        updated_ = false;
    return fired;
}

// Diagnostics
std::size_t PriceTable::symbolCount() const {
    std::shared_lock<std::shared_mutex> lk(dataMu_);
    return table_.size();
}

std::size_t PriceTable::exchangeCount(const std::string &symbol) const {
    std::shared_lock<std::shared_mutex> lk(dataMu_);
    auto it = table_.find(symbol);
    return (it != table_.end()) ? it->second.size() : 0;
}

uint64_t PriceTable::totalUpdates() const noexcept {
    return totalUpdates_.load(std::memory_order_relaxed);
}

} // namespace orbit