#pragma once

#include "Exchange.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace orbit {

class KrakenWS final : public ExchangeWS {
  public:
    KrakenWS(net::io_context &ioc, ssl::context &ssl_ctx,
             PriceTable &priceTable, const Config &cfg,
             std::vector<std::string> symbols);

    const char *host() const noexcept override { return "ws.kraken.com"; }
    const char *port() const noexcept override { return "443"; }
    const char *path() const noexcept override { return "/"; }

    std::string buildSubscribeMsg() override;
    void onMessage(const std::string &msg) override;

  private:
    std::vector<std::string> symbols_;

    // Bidirectional map: canonical symbol ↔ Kraken pair string
    std::unordered_map<std::string, std::string> toKraken_; // BTCUSDT → XBT/USD
    std::unordered_map<std::string, std::string>
        fromKraken_; // XBT/USD → BTCUSDT

    void buildMaps();
    static std::string krakenPair(const std::string &canonical);
};

} // namespace orbit