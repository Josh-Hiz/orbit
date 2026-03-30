// HyperLiquid l2Book WebSocket client.
//
// One subscription per coin:
//   {"method":"subscribe","subscription":{"type":"l2Book","coin":"BTC"}}
//
// l2Book update message:
//   {
//     "channel": "l2Book",
//     "data": {
//       "coin": "BTC",
//       "time": 1700000000000,
//       "levels": [
//         [{"px":"29000.0","sz":"1.5","n":3}, ...],   // bids (index 0), best =
//         first entry
//         [{"px":"29001.0","sz":"0.8","n":1}, ...]    // asks (index 1), best =
//         first entry
//       ]
//     }
//   }
//
// Note: "n" is the number of orders at that level.
// levels[0] = bids sorted descending (highest first = best bid)
// levels[1] = asks sorted ascending  (lowest first  = best ask)

#include "HyperLiquidWS.h"
#include "utils/Logger.hpp"

#include <nlohmann/json.hpp>

namespace orbit {

using json = nlohmann::json;

// Symbol mapping
static const std::unordered_map<std::string, std::string> kHLMap = {
    {"BTCUSDT", "BTC"},   {"ETHUSDT", "ETH"}, {"SOLUSDT", "SOL"},
    {"XRPUSDT", "XRP"},   {"ADAUSDT", "ADA"}, {"DOGEUSDT", "DOGE"},
    {"AVAXUSDT", "AVAX"}, {"DOTUSDT", "DOT"}, {"MATICUSDT", "MATIC"},
    {"LINKUSDT", "LINK"}, {"UNIUSDT", "UNI"}, {"LTCUSDT", "LTC"},
    {"BCHUSDT", "BCH"},
};

// Construction
HyperLiquidWS::HyperLiquidWS(net::io_context &ioc, ssl::context &ssl_ctx,
                             PriceTable &priceTable, const Config &cfg,
                             std::vector<std::string> symbols)
    : ExchangeWS(ioc, ssl_ctx, priceTable, cfg, "HyperLiquid"),
      symbols_(std::move(symbols)) {
    buildMaps();
}

void HyperLiquidWS::buildMaps() {
    for (const auto &sym : symbols_) {
        auto it = kHLMap.find(sym);
        if (it != kHLMap.end()) {
            toHL_[sym] = it->second;
            fromHL_[it->second] = sym;
        } else {
            LOG_WARN("HyperLiquid", "No HL coin mapping for: ", sym);
        }
    }
}

// Subscription
// HL requires one subscription message per coin.
std::string HyperLiquidWS::buildSubscribeMsg() {
    // Fallback (used by base class if buildSubscribeMsgs not overridden).
    if (symbols_.empty())
        return "{}";
    return json{
        {"method", "subscribe"},
        {"subscription", {{"type", "l2Book"}, {"coin", toHL_.begin()->second}}}}
        .dump();
}

std::vector<std::string> HyperLiquidWS::buildSubscribeMsgs() {
    std::vector<std::string> msgs;
    msgs.reserve(toHL_.size());
    for (const auto &sym : symbols_) {
        auto it = toHL_.find(sym);
        if (it == toHL_.end())
            continue;
        msgs.push_back(
            json{{"method", "subscribe"},
                 {"subscription", {{"type", "l2Book"}, {"coin", it->second}}}}
                .dump());
    }
    return msgs;
}

// Message handler
void HyperLiquidWS::onMessage(const std::string &msg) {
    auto j = json::parse(msg, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return;

    std::string channel = j.value("channel", "");

    // Pong response to keep-alive pings.
    if (channel == "pong")
        return;

    if (channel != "l2Book")
        return;

    if (!j.contains("data") || !j["data"].is_object())
        return;
    const auto &data = j["data"];

    std::string coin = data.value("coin", "");
    auto it = fromHL_.find(coin);
    if (it == fromHL_.end())
        return;

    const std::string &symbol = it->second;

    if (!data.contains("levels") || !data["levels"].is_array())
        return;
    const auto &levels = data["levels"];
    if (levels.size() < 2)
        return;

    // levels[0] = bids (best bid = first element)
    // levels[1] = asks (best ask = first element)
    const auto &bids = levels[0];
    const auto &asks = levels[1];

    if (!bids.is_array() || bids.empty())
        return;
    if (!asks.is_array() || asks.empty())
        return;

    // Parse best bid.
    const auto &bestBidLevel = bids[0];
    const auto &bestAskLevel = asks[0];

    if (!bestBidLevel.contains("px") || !bestAskLevel.contains("px"))
        return;

    double bid = std::stod(bestBidLevel["px"].get<std::string>());
    double ask = std::stod(bestAskLevel["px"].get<std::string>());
    double bidQty = bestBidLevel.contains("sz")
                        ? std::stod(bestBidLevel["sz"].get<std::string>())
                        : 0.0;
    double askQty = bestAskLevel.contains("sz")
                        ? std::stod(bestAskLevel["sz"].get<std::string>())
                        : 0.0;

    // HyperLiquid provides a millisecond timestamp in "time".
    int64_t tsUs =
        data.contains("time")
            ? static_cast<int64_t>(data["time"].get<int64_t>()) * 1000LL
            : 0LL;

    priceTable_.update(symbol, name_, bid, ask, bidQty, askQty, tsUs);
}

std::string HyperLiquidWS::hlCoin(const std::string &canonical) {
    auto it = kHLMap.find(canonical);
    return (it != kHLMap.end()) ? it->second : "";
}

} // namespace orbit