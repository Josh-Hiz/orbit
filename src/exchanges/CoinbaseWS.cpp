#include "CoinbaseWS.h"
#include "../utils/Logger.h"

#include <nlohmann/json.hpp>

namespace orbit {

using json = nlohmann::json;

// Symbol mapping
static const std::unordered_map<std::string, std::string> kCoinbaseMap = {
    {"BTCUSDT", "BTC-USD"},     {"ETHUSDT", "ETH-USD"},
    {"SOLUSDT", "SOL-USD"},     {"XRPUSDT", "XRP-USD"},
    {"ADAUSDT", "ADA-USD"},     {"DOGEUSDT", "DOGE-USD"},
    {"AVAXUSDT", "AVAX-USD"},   {"DOTUSDT", "DOT-USD"},
    {"MATICUSDT", "MATIC-USD"}, {"LINKUSDT", "LINK-USD"},
    {"UNIUSDT", "UNI-USD"},     {"LTCUSDT", "LTC-USD"},
    {"BCHUSDT", "BCH-USD"},
};

// Construction
CoinbaseWS::CoinbaseWS(net::io_context &ioc, ssl::context &ssl_ctx,
                       PriceTable &priceTable, const Config &cfg,
                       std::vector<std::string> symbols)
    : ExchangeWS(ioc, ssl_ctx, priceTable, cfg, "Coinbase"),
      symbols_(std::move(symbols)) {
    buildMaps();
}

void CoinbaseWS::buildMaps() {
    for (const auto &sym : symbols_) {
        auto it = kCoinbaseMap.find(sym);
        if (it != kCoinbaseMap.end()) {
            toCoinbase_[sym] = it->second;
            fromCoinbase_[it->second] = sym;
        } else {
            LOG_WARN("Coinbase", "No Coinbase product_id mapping for: ", sym);
        }
    }
}

// Subscription
std::string CoinbaseWS::buildSubscribeMsg() {
    json product_ids = json::array();
    for (const auto &sym : symbols_) {
        auto it = toCoinbase_.find(sym);
        if (it != toCoinbase_.end())
            product_ids.push_back(it->second);
    }

    return json{{"type", "subscribe"},
                {"product_ids", product_ids},
                {"channel", "ticker"}}
        .dump();
}

// Message handler
void CoinbaseWS::onMessage(const std::string &msg) {
    auto j = json::parse(msg, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return;

    if (!j.contains("channel"))
        return;
    std::string channel = j["channel"].get<std::string>();

    if (channel == "subscriptions") {
        LOG_DEBUG("Coinbase", "Subscriptions ACK received");
        return;
    }

    if (channel != "ticker")
        return;

    // events is an array; each event has type and tickers array.
    if (!j.contains("events") || !j["events"].is_array())
        return;

    for (const auto &event : j["events"]) {
        if (!event.is_object())
            continue;

        // type can be "snapshot" or "update" – handle both.
        std::string type = event.value("type", "");
        if (type != "update" && type != "snapshot")
            continue;

        if (!event.contains("tickers") || !event["tickers"].is_array())
            continue;

        for (const auto &ticker : event["tickers"]) {
            if (!ticker.is_object())
                continue;

            std::string pid = ticker.value("product_id", "");
            auto it = fromCoinbase_.find(pid);
            if (it == fromCoinbase_.end())
                continue;

            const std::string &symbol = it->second;

            // best_bid / best_ask are the fields we want.
            if (!ticker.contains("best_bid") || !ticker.contains("best_ask"))
                continue;

            std::string bidStr = ticker["best_bid"].get<std::string>();
            std::string askStr = ticker["best_ask"].get<std::string>();
            if (bidStr.empty() || askStr.empty())
                continue;

            double bid = std::stod(bidStr);
            double ask = std::stod(askStr);
            double bidQty =
                ticker.contains("best_bid_quantity")
                    ? std::stod(ticker["best_bid_quantity"].get<std::string>())
                    : 0.0;
            double askQty =
                ticker.contains("best_ask_quantity")
                    ? std::stod(ticker["best_ask_quantity"].get<std::string>())
                    : 0.0;

            priceTable_.update(symbol, name_, bid, ask, bidQty, askQty);
        }
    }
}

} // namespace orbit