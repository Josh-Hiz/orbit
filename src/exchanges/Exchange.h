#pragma once

#include "../core/PriceTable.h"
#include "../utils/Config.h"
#include "../utils/Logger.h"
#include "../utils/Metrics.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace orbit {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = net::ip::tcp;
using error_code = boost::system::error_code;

// WebSocket stream type
using WsStream = beast::websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

// Base exchange client
class ExchangeWS : public std::enable_shared_from_this<ExchangeWS> {
  public:
    ExchangeWS(net::io_context &ioc, ssl::context &ssl_ctx,
               PriceTable &priceTable, const Config &cfg, std::string name)
        : ioc_(ioc), ssl_ctx_(ssl_ctx), priceTable_(priceTable), cfg_(cfg),
          name_(std::move(name)), resolver_(net::make_strand(ioc)),
          ws_(net::make_strand(ioc), ssl_ctx_),
          reconnectMs_(cfg.reconnectDelayMs) {}

    virtual ~ExchangeWS() = default;

    // Interface for subclasses
    virtual const char *host() const noexcept = 0;
    virtual const char *port() const noexcept = 0;
    virtual const char *path() const noexcept = 0;
    virtual std::string buildSubscribeMsg() = 0;
    virtual void onMessage(const std::string &msg) = 0;
    // Some exchanges need >1 subscription message.
    virtual std::vector<std::string> buildSubscribeMsgs() {
        return {buildSubscribeMsg()};
    }

    // Lifecycle
    void connect() {
        LOG_INFO(name_, "Connecting to ", host(), ":", port(), path());
        resolver_.async_resolve(
            host(), port(),
            beast::bind_front_handler(&ExchangeWS::onResolve,
                                      shared_from_this()));
    }

    void close() {
        closing_.store(true, std::memory_order_relaxed);
        error_code ec;
        beast::get_lowest_layer(ws_).cancel(ec);
    }

    const std::string &exchangeName() const noexcept { return name_; }
    bool isConnected() const noexcept { return connected_.load(); }

  protected:
    void onResolve(error_code ec, tcp::resolver::results_type results) {
        if (ec)
            return scheduleReconnect(ec, "resolve");
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));
        beast::get_lowest_layer(ws_).async_connect(
            results, beast::bind_front_handler(&ExchangeWS::onConnect,
                                               shared_from_this()));
    }

    void onConnect(error_code ec, tcp::resolver::results_type::endpoint_type) {
        if (ec)
            return scheduleReconnect(ec, "connect");
        beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(10));

        // Set SNI hostname for TLS.
        if (!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(),
                                      host())) {
            ec = beast::error_code(static_cast<int>(::ERR_get_error()),
                                   net::error::get_ssl_category());
            return scheduleReconnect(ec, "SNI");
        }

        ws_.next_layer().async_handshake(
            ssl::stream_base::client,
            beast::bind_front_handler(&ExchangeWS::onSslHandshake,
                                      shared_from_this()));
    }

    void onSslHandshake(error_code ec) {
        if (ec)
            return scheduleReconnect(ec, "ssl_handshake");
        beast::get_lowest_layer(ws_).expires_never();

        // Tune WebSocket options for low latency.
        ws_.set_option(beast::websocket::stream_base::timeout::suggested(
            beast::role_type::client));
        ws_.set_option(beast::websocket::stream_base::decorator(
            [&](beast::websocket::request_type &req) {
                req.set(beast::http::field::user_agent,
                        "orbit/0.2 boost.beast");
            }));

        ws_.async_handshake(
            host(), path(),
            beast::bind_front_handler(&ExchangeWS::onWsHandshake,
                                      shared_from_this()));
    }

    void onWsHandshake(error_code ec) {
        if (ec)
            return scheduleReconnect(ec, "ws_handshake");

        connected_.store(true, std::memory_order_relaxed);
        LOG_INFO(name_, "WebSocket connected");

        // Send all subscription messages sequentially.
        subMsgs_ = buildSubscribeMsgs();
        sendNextSubscription();
    }

    // Send subscription messages one at a time.
    void sendNextSubscription() {
        if (subIdx_ >= subMsgs_.size()) {
            doRead(); // all subs sent – start reading
            return;
        }
        ws_.async_write(net::buffer(subMsgs_[subIdx_++]),
                        beast::bind_front_handler(&ExchangeWS::onWrite,
                                                  shared_from_this()));
    }

    void onWrite(error_code ec, std::size_t) {
        if (ec)
            return scheduleReconnect(ec, "write");
        sendNextSubscription();
    }

    void doRead() {
        buf_.consume(buf_.size()); // clear old data
        ws_.async_read(buf_, beast::bind_front_handler(&ExchangeWS::onRead,
                                                       shared_from_this()));
    }

    void onRead(error_code ec, std::size_t) {
        if (ec) {
            connected_.store(false, std::memory_order_relaxed);
            return scheduleReconnect(ec, "read");
        }

        std::string msg = beast::buffers_to_string(buf_.data());
        buf_.consume(buf_.size());

        Metrics::instance().onMessage(name_);

        try {
            onMessage(msg);
        } catch (const std::exception &e) {
            LOG_WARN(name_, "Parse error: ", e.what());
        }

        doRead(); // loop
    }

    // Reconnect with exponential back-off
    void scheduleReconnect(error_code ec, const char *where) {
        if (closing_.load(std::memory_order_relaxed))
            return;

        LOG_WARN(name_, "Error [", where, "]: ", ec.message(),
                 " – reconnecting in ", reconnectMs_, "ms");

        // Reset the stream before reconnecting.
        ws_ = WsStream(net::make_strand(ioc_), ssl_ctx_);
        subIdx_ = 0;
        subMsgs_.clear();
        connected_.store(false, std::memory_order_relaxed);

        auto timer = std::make_shared<net::steady_timer>(
            ioc_, std::chrono::milliseconds(reconnectMs_));
        reconnectMs_ = std::min(reconnectMs_ * 2, 30'000L); // cap at 30 s

        timer->async_wait([self = shared_from_this(), timer](error_code) {
            self->reconnectMs_ =
                self->cfg_.reconnectDelayMs; // reset after successful reconnect
            self->connect();
        });
    }

    // Members
    net::io_context &ioc_;
    ssl::context &ssl_ctx_;
    PriceTable &priceTable_;
    Config cfg_;
    std::string name_;

    tcp::resolver resolver_;
    WsStream ws_;
    beast::flat_buffer buf_;

    std::vector<std::string> subMsgs_;
    std::size_t subIdx_{0};

    std::atomic<bool> connected_{false};
    std::atomic<bool> closing_{false};
    long reconnectMs_;
};

} // namespace orbit