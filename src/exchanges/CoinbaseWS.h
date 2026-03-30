#pragma once

// Coinbase Advanced Trade WebSocket client – subscribes to the "ticker"
// channel.
//
// WS endpoint : wss://advanced-trade-ws.coinbase.com
// Channel     : ticker
// Docs        :
// https://docs.cdp.coinbase.com/advanced-trade/docs/ws-channels#ticker-channel
//
// Coinbase product IDs use hyphen notation: BTC-USD, ETH-USD, SOL-USD.

#include "Exchange.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace orbit {

class CoinbaseWS final : public ExchangeWS {
  public:
    CoinbaseWS(net::io_context &ioc, ssl::context &ssl_ctx,
               PriceTable &priceTable, const Config &cfg,
               std::vector<std::string> symbols);

    const char *host() const noexcept override {
        return "advanced-trade-ws.coinbase.com";
    }
    const char *port() const noexcept override { return "443"; }
    const char *path() const noexcept override { return "/"; }

    std::string buildSubscribeMsg() override;
    void onMessage(const std::string &msg) override;

  private:
    std::vector<std::string> symbols_;

    // Maps canonical symbol ↔ Coinbase product_id.
    std::unordered_map<std::string, std::string>
        toCoinbase_; // BTCUSDT → BTC-USD
    std::unordered_map<std::string, std::string>
        fromCoinbase_; // BTC-USD → BTCUSDT

    void buildMaps();
};

} // namespace orbit