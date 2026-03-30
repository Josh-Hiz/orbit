#pragma once

#include "Exchange.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace orbit {

class HyperLiquidWS final : public ExchangeWS {
  public:
    HyperLiquidWS(net::io_context &ioc, ssl::context &ssl_ctx,
                  PriceTable &priceTable, const Config &cfg,
                  std::vector<std::string> symbols);

    const char *host() const noexcept override { return "api.hyperliquid.xyz"; }
    const char *port() const noexcept override { return "443"; }
    const char *path() const noexcept override { return "/ws"; }

    // HyperLiquid requires one subscription message per coin.
    std::string buildSubscribeMsg() override;
    std::vector<std::string> buildSubscribeMsgs() override;
    void onMessage(const std::string &msg) override;

  private:
    std::vector<std::string> symbols_;

    // Maps canonical symbol → HL coin name (and back).
    std::unordered_map<std::string, std::string> toHL_;   // BTCUSDT → BTC
    std::unordered_map<std::string, std::string> fromHL_; // BTC → BTCUSDT

    void buildMaps();
    static std::string hlCoin(const std::string &canonical);
};

} // namespace orbit