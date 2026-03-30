#include "BinanceWS.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace orbit {

using json = nlohmann::json;

// Construction
BinanceWS::BinanceWS(net::io_context &ioc, ssl::context &ssl_ctx,
                     PriceTable &priceTable, const Config &cfg,
                     std::vector<std::string> symbols)
    : ExchangeWS(ioc, ssl_ctx, priceTable, cfg, "Binance"),
      symbols_(std::move(symbols)) {}

// Subscription
std::string BinanceWS::buildSubscribeMsg() {
    // Binance allows up to 1024 subscriptions per connection.
    json params = json::array();
    for (const auto &s : symbols_)
        params.push_back(normalise(s) + "@bookTicker");

    return json{{"method", "SUBSCRIBE"}, {"params", params}, {"id", 1}}.dump();
}

std::vector<std::string> BinanceWS::buildSubscribeMsgs() {
    // Single combined subscription.
    return {buildSubscribeMsg()};
}

// Message handler
void BinanceWS::onMessage(const std::string &msg) {
    auto j = json::parse(msg, nullptr, /*exceptions=*/false);
    if (!j.is_object())
        return;

    // Ignore subscription ACK {"result":null,"id":1}
    if (j.contains("result"))
        return;
    // Ignore combined stream wrapper (not used here but kept for robustness)
    if (j.contains("stream"))
        return;

    // Required fields
    if (!j.contains("s") || !j.contains("b") || !j.contains("a"))
        return;

    std::string symbol = j["s"].get<std::string>(); // e.g. "BTCUSDT"
    double bid = std::stod(j["b"].get<std::string>());
    double ask = std::stod(j["a"].get<std::string>());
    double bidQty =
        j.contains("B") ? std::stod(j["B"].get<std::string>()) : 0.0;
    double askQty =
        j.contains("A") ? std::stod(j["A"].get<std::string>()) : 0.0;

    priceTable_.update(symbol, name_, bid, ask, bidQty, askQty);
}

// Helpers
std::string BinanceWS::normalise(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

} // namespace orbit