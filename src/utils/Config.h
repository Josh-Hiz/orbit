#pragma once

#include <cstdlib>
#include <string>
#include <vector>

namespace orbit {

struct Config {
    // Arbitrage detection threshold (percentage, e.g. 0.1 = 0.1%)
    double minSpreadPct{0.10};

    // Stale quote age – quotes older than this are ignored (milliseconds)
    long staleMs{5000};

    // Reconnect delay after WS error (milliseconds)
    long reconnectDelayMs{3000};

    // Symbols to monitor (normalised: uppercase, no separator)
    std::vector<std::string> symbols{"BTCUSDT", "ETHUSDT", "SOLUSDT"};

    // Which exchanges to enable
    bool enableBinance{true};
    bool enableKraken{true};
    bool enableCoinbase{true};
    bool enableHyperLiquid{true};

    // Log level: TRACE DEBUG INFO WARN ERROR FATAL
    std::string logLevel{"INFO"};

    // Metrics print interval (seconds, 0 = disabled)
    int metricsIntervalSec{30};

    // Load from environment
    static Config fromEnv() {
        Config c;
        if (auto *v = std::getenv("ORBIT_MIN_SPREAD_PCT"))
            c.minSpreadPct = std::stod(v);
        if (auto *v = std::getenv("ORBIT_STALE_MS"))
            c.staleMs = std::stol(v);
        if (auto *v = std::getenv("ORBIT_RECONNECT_MS"))
            c.reconnectDelayMs = std::stol(v);
        if (auto *v = std::getenv("ORBIT_LOG_LEVEL"))
            c.logLevel = v;
        if (auto *v = std::getenv("ORBIT_METRICS_INTERVAL"))
            c.metricsIntervalSec = std::stoi(v);

        if (auto *v = std::getenv("ORBIT_ENABLE_BINANCE"))
            c.enableBinance = std::string(v) != "0";
        if (auto *v = std::getenv("ORBIT_ENABLE_KRAKEN"))
            c.enableKraken = std::string(v) != "0";
        if (auto *v = std::getenv("ORBIT_ENABLE_COINBASE"))
            c.enableCoinbase = std::string(v) != "0";
        if (auto *v = std::getenv("ORBIT_ENABLE_HYPERLIQUID"))
            c.enableHyperLiquid = std::string(v) != "0";

        // Comma-separated symbol list "BTCUSDT,ETHUSDT"
        if (auto *v = std::getenv("ORBIT_SYMBOLS")) {
            c.symbols.clear();
            std::string sv(v);
            std::string tok;
            for (char ch : sv) {
                if (ch == ',') {
                    if (!tok.empty()) {
                        c.symbols.push_back(tok);
                        tok.clear();
                    }
                } else {
                    tok += ch;
                }
            }
            if (!tok.empty())
                c.symbols.push_back(tok);
        }
        return c;
    }
};

} // namespace orbit