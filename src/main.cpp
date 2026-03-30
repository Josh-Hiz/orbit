#include "core/ArbitrageEngine.h"
#include "core/PriceTable.h"
#include "exchanges/BinanceWS.h"
#include "exchanges/CoinbaseWS.h"
#include "exchanges/HyperLiquidWS.h"
#include "exchanges/KrakenWS.h"
#include "utils/Config.h"
#include "utils/Logger.h"
#include "utils/Metrics.h"

#include <boost/asio.h>
#include <boost/asio/ssl.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <thread>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using orbit::Logger;
using orbit::LogLevel;

//  shutdown flag
static std::atomic<bool> g_shutdown{false};

static void onSignal(int) { g_shutdown.store(true, std::memory_order_relaxed); }

// one io_context in its own thread
struct IoThread {
    net::io_context ioc{1}; // concurrency_hint=1 (single thread)
    net::executor_work_guard<net::io_context::executor_type> work =
        net::make_work_guard(ioc);
    std::thread thread;

    void start() {
        thread = std::thread([this] { ioc.run(); });
    }
    void stop() {
        work.reset();
        ioc.stop();
        if (thread.joinable())
            thread.join();
    }
};

// SSL context
static ssl::context makeSslContext() {
    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_peer);
    ctx.set_default_verify_paths(); // use OS CA store
    return ctx;
}

// Log level from config string
static LogLevel parseLogLevel(const std::string &s) {
    if (s == "TRACE")
        return LogLevel::TRACE;
    if (s == "DEBUG")
        return LogLevel::DEBUG;
    if (s == "WARN")
        return LogLevel::WARN;
    if (s == "ERROR")
        return LogLevel::ERROR;
    if (s == "FATAL")
        return LogLevel::FATAL;
    return LogLevel::INFO;
}

int main() {
    // Config
    auto cfg = orbit::Config::fromEnv();
    Logger::setLevel(parseLogLevel(cfg.logLevel));

    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "  Orbit - Crypto Arbitrage Detector    ");
    LOG_INFO("Main", "  v1.0  |  C++17 / Boost.Beast / SSL  ");
    LOG_INFO("Main", "========================================");
    LOG_INFO("Main", "Symbols   : ", [&] {
        std::string s;
        for (auto &sym : cfg.symbols)
            s += sym + " ";
        return s;
    }());
    LOG_INFO("Main", "Min spread: ", cfg.minSpreadPct, "%");
    LOG_INFO("Main", "Stale age : ", cfg.staleMs, " ms");

    // Signal handling
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    // Shared state
    orbit::PriceTable priceTable;

    // SSL contexr
    auto ssl_ctx = makeSslContext();

    // Exchange I/O threads
    // Each exchange gets its own io_context to ensure isolation.
    IoThread binanceIo, krakenIo, coinbaseIo, hlIo;

    // Construct exchange clients
    std::shared_ptr<orbit::BinanceWS> binance;
    std::shared_ptr<orbit::KrakenWS> kraken;
    std::shared_ptr<orbit::CoinbaseWS> coinbase;
    std::shared_ptr<orbit::HyperLiquidWS> hl;

    if (cfg.enableBinance) {
        binance = std::make_shared<orbit::BinanceWS>(
            binanceIo.ioc, ssl_ctx, priceTable, cfg, cfg.symbols);
        LOG_INFO("Main", "Binance    : ENABLED");
    }
    if (cfg.enableKraken) {
        kraken = std::make_shared<orbit::KrakenWS>(
            krakenIo.ioc, ssl_ctx, priceTable, cfg, cfg.symbols);
        LOG_INFO("Main", "Kraken     : ENABLED");
    }
    if (cfg.enableCoinbase) {
        coinbase = std::make_shared<orbit::CoinbaseWS>(
            coinbaseIo.ioc, ssl_ctx, priceTable, cfg, cfg.symbols);
        LOG_INFO("Main", "Coinbase   : ENABLED");
    }
    if (cfg.enableHyperLiquid) {
        hl = std::make_shared<orbit::HyperLiquidWS>(
            hlIo.ioc, ssl_ctx, priceTable, cfg, cfg.symbols);
        LOG_INFO("Main", "HyperLiquid: ENABLED");
    }

    // Arbitrage engine
    orbit::ArbitrageEngine engine(priceTable, cfg);
    engine.setCallback(
        [](const orbit::ArbitrageOpportunity &opp) { (void)opp; });
    engine.start();

    // Start I/O threads and kick off connections
    binanceIo.start();
    krakenIo.start();
    coinbaseIo.start();
    hlIo.start();

    // Schedule connections on the respective io_context strands.
    if (binance)
        net::post(binanceIo.ioc, [&] { binance->connect(); });
    if (kraken)
        net::post(krakenIo.ioc, [&] { kraken->connect(); });
    if (coinbase)
        net::post(coinbaseIo.ioc, [&] { coinbase->connect(); });
    if (hl)
        net::post(hlIo.ioc, [&] { hl->connect(); });

    LOG_INFO("Main", "All exchanges connecting. Press Ctrl+C to quit.");

    // ── Metrics loop ──────────────────────────────────────────────────────
    auto metricsInterval = std::chrono::seconds(cfg.metricsIntervalSec);
    auto nextMetrics = std::chrono::steady_clock::now() + metricsInterval;

    // ── Main wait loop ────────────────────────────────────────────────────
    while (!g_shutdown.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (cfg.metricsIntervalSec > 0) {
            auto now = std::chrono::steady_clock::now();
            if (now >= nextMetrics) {
                nextMetrics = now + metricsInterval;
                auto &m = orbit::Metrics::instance();
                if (cfg.enableBinance)
                    m.printSummary("Binance");
                if (cfg.enableKraken)
                    m.printSummary("Kraken");
                if (cfg.enableCoinbase)
                    m.printSummary("Coinbase");
                if (cfg.enableHyperLiquid)
                    m.printSummary("HyperLiquid");

                LOG_INFO("Main", "Price table: ", priceTable.symbolCount(),
                         " symbols | ", priceTable.totalUpdates(),
                         " total updates | ", engine.totalOpportunities(),
                         " opportunities");
            }
        }
    }

    // shutdown
    LOG_INFO("Main", "Shutting down…");

    engine.stop();

    if (binance)
        binance->close();
    if (kraken)
        kraken->close();
    if (coinbase)
        coinbase->close();
    if (hl)
        hl->close();

    binanceIo.stop();
    krakenIo.stop();
    coinbaseIo.stop();
    hlIo.stop();

    LOG_INFO("Main", "Orbit stopped. Total opportunities detected: ",
             engine.totalOpportunities());

    return 0;
}