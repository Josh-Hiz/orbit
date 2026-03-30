#pragma once

// Binance WebSocket client – subscribes to <symbol>@bookTicker streams.
// WS endpoint : wss://stream.binance.com:9443/ws
// Stream      : <symbol>@bookTicker  (best bid/ask, real-time, no throttle)
// Docs        :
// https://developers.binance.com/docs/binance-spot-api-docs/web-socket-streams

#include "Exchange.h"
#include <string>
#include <vector>

namespace orbit {

class BinanceWS final : public ExchangeWS {
  public:
    BinanceWS(net::io_context &ioc, ssl::context &ssl_ctx,
              PriceTable &priceTable, const Config &cfg,
              std::vector<std::string> symbols);

    const char *host() const noexcept override { return "stream.binance.com"; }
    const char *port() const noexcept override { return "9443"; }
    const char *path() const noexcept override { return "/ws"; }

    std::string buildSubscribeMsg() override;
    std::vector<std::string> buildSubscribeMsgs() override;
    void onMessage(const std::string &msg) override;

  private:
    std::vector<std::string> symbols_;

    // Normalise to lowercase BTCUSDT → "btcusdt"
    static std::string normalise(std::string s);
};

} // namespace orbit