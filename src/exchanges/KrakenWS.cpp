#include "KrakenWS.h"
#include "../utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace orbit {

using json = nlohmann::json;

// Symbol mapping table
//  Canonical (BTCUSDT) → Kraken pair (XBT/USD). Kraken uses XBT for Bitcoin and
//  "/" as separator.
static const std::unordered_map<std::string, std::string> kSymbolMap = {
    {"BTCUSDT", "XBT/USD"},     {"ETHUSDT", "ETH/USD"},
    {"SOLUSDT", "SOL/USD"},     {"XRPUSDT", "XRP/USD"},
    {"ADAUSDT", "ADA/USD"},     {"DOGEUSDT", "DOGE/USD"},
    {"AVAXUSDT", "AVAX/USD"},   {"DOTUSDT", "DOT/USD"},
    {"MATICUSDT", "MATIC/USD"}, {"LINKUSDT", "LINK/USD"},
    {"UNIUSDT", "UNI/USD"},     {"LTCUSDT", "LTC/USD"},
    {"BCHUSDT", "BCH/USD"},
};

// Construction
KrakenWS::KrakenWS(net::io_context &ioc, ssl::context &ssl_ctx,
                   PriceTable &priceTable, const Config &cfg,
                   std::vector<std::string> symbols)
    : ExchangeWS(ioc, ssl_ctx, priceTable, cfg, "Kraken"),
      symbols_(std::move(symbols)) {
    buildMaps();
}

void KrakenWS::buildMaps() {
    for (const auto &sym : symbols_) {
        auto it = kSymbolMap.find(sym);
        if (it != kSymbolMap.end()) {
            toKraken_[sym] = it->second;
            fromKraken_[it->second] = sym;
        } else {
            LOG_WARN("Kraken", "No Kraken pair mapping for symbol: ", sym);
        }
    }
}

// Subscription
std::string KrakenWS::buildSubscribeMsg() {
    json pairs = json::array();
    for (const auto &sym : symbols_) {
        auto it = toKraken_.find(sym);
        if (it != toKraken_.end())
            pairs.push_back(it->second);
    }

    return json{{"event", "subscribe"},
                {"pair", pairs},
                {"subscription", {{"name", "ticker"}}}}
        .dump();
}

// Message handler
void KrakenWS::onMessage(const std::string &msg) {
    auto j = json::parse(msg, nullptr, false);
    if (j.is_discarded())
        return;

    // Event messages (heartbeat, subscriptionStatus, etc.) are objects.
    if (j.is_object()) {
        if (j.contains("event")) {
            std::string ev = j["event"].get<std::string>();
            if (ev == "subscriptionStatus") {
                LOG_DEBUG("Kraken", "subscriptionStatus: ", j.dump());
            } else if (ev == "heartbeat") {
                // no-op
            } else if (ev == "error") {
                LOG_ERROR("Kraken", "Server error: ", j.dump());
            }
        }
        return;
    }

    // Ticker data is a 4-element array: [channelID, data, "ticker", "XBT/USD"]
    if (!j.is_array() || j.size() < 4)
        return;

    // Validate channel name.
    if (!j[2].is_string() || j[2].get<std::string>() != "ticker")
        return;
    if (!j[3].is_string())
        return;

    std::string krakenPair = j[3].get<std::string>();
    auto it = fromKraken_.find(krakenPair);
    if (it == fromKraken_.end())
        return; // unknown pair

    const std::string &symbol = it->second; // e.g. "BTCUSDT"
    const auto &data = j[1];

    // "b" = bid [price, wholeLotVol, lotVol], "a" = ask [price, ...]
    if (!data.contains("b") || !data.contains("a"))
        return;

    const auto &bidArr = data["b"];
    const auto &askArr = data["a"];
    if (!bidArr.is_array() || bidArr.empty())
        return;
    if (!askArr.is_array() || askArr.empty())
        return;

    double bid = std::stod(bidArr[0].get<std::string>());
    double ask = std::stod(askArr[0].get<std::string>());
    double bidQty =
        bidArr.size() > 2 ? std::stod(bidArr[2].get<std::string>()) : 0.0;
    double askQty =
        askArr.size() > 2 ? std::stod(askArr[2].get<std::string>()) : 0.0;

    priceTable_.update(symbol, name_, bid, ask, bidQty, askQty);
}

std::string KrakenWS::krakenPair(const std::string &canonical) {
    auto it = kSymbolMap.find(canonical);
    return (it != kSymbolMap.end()) ? it->second : "";
}

} // namespace orbit