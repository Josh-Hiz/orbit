#pragma once

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <string_view>

namespace orbit {

enum class LogLevel : int { TRACE = 0, DEBUG, INFO, WARN, ERROR, FATAL };

class Logger {
  public:
    // Configuration
    static void setLevel(LogLevel lvl) noexcept {
        level_.store(static_cast<int>(lvl));
    }
    static LogLevel getLevel() noexcept {
        return static_cast<LogLevel>(level_.load());
    }

    // Core log function
    static void log(LogLevel lvl, std::string_view component,
                    std::string_view msg) {
        if (static_cast<int>(lvl) < level_.load())
            return;

        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      now.time_since_epoch())
                      .count();
        long long s = us / 1'000'000LL;
        long long us_rem = us % 1'000'000LL;

        std::lock_guard<std::mutex> lk(mutex_);
        std::fprintf(stderr, "%s[%lld.%06lld] [%-5s] [%-12s] %s%s\n",
                     colour(lvl), s, us_rem, levelStr(lvl), component.data(),
                     msg.data(), RESET);
    }

    // Convenience helpers
    template <typename... Args>
    static void trace(std::string_view comp, Args &&...args) {
        log(LogLevel::TRACE, comp, concat(std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void debug(std::string_view comp, Args &&...args) {
        log(LogLevel::DEBUG, comp, concat(std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void info(std::string_view comp, Args &&...args) {
        log(LogLevel::INFO, comp, concat(std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void warn(std::string_view comp, Args &&...args) {
        log(LogLevel::WARN, comp, concat(std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void error(std::string_view comp, Args &&...args) {
        log(LogLevel::ERROR, comp, concat(std::forward<Args>(args)...));
    }
    template <typename... Args>
    static void fatal(std::string_view comp, Args &&...args) {
        log(LogLevel::FATAL, comp, concat(std::forward<Args>(args)...));
    }

  private:
    static constexpr const char *RESET = "\033[0m";

    static const char *colour(LogLevel lvl) noexcept {
        switch (lvl) {
        case LogLevel::TRACE:
            return "\033[37m";
        case LogLevel::DEBUG:
            return "\033[36m";
        case LogLevel::INFO:
            return "\033[32m";
        case LogLevel::WARN:
            return "\033[33m";
        case LogLevel::ERROR:
            return "\033[31m";
        case LogLevel::FATAL:
            return "\033[35m";
        }
        return "";
    }

    static const char *levelStr(LogLevel lvl) noexcept {
        switch (lvl) {
        case LogLevel::TRACE:
            return "TRACE";
        case LogLevel::DEBUG:
            return "DEBUG";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::WARN:
            return "WARN";
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::FATAL:
            return "FATAL";
        }
        return "???";
    }

    // String conversion helpers
    static std::string str(std::string s) { return s; }
    static std::string str(std::string_view sv) { return std::string(sv); }
    static std::string str(const char *c) { return c ? c : ""; }
    static std::string str(char c) { return std::string(1, c); }
    static std::string str(bool v) { return v ? "true" : "false"; }
    template <typename T> static std::string str(T &&v) {
        return std::to_string(std::forward<T>(v));
    }

    // Simple variadic concat – avoids std::ostringstream overhead for small
    // msgs.
    static std::string concat() { return {}; }
    template <typename T, typename... Rest>
    static std::string concat(T &&first, Rest &&...rest) {
        return str(std::forward<T>(first)) +
               concat(std::forward<Rest>(rest)...);
    }

    static inline std::mutex mutex_;
    static inline std::atomic<int> level_{static_cast<int>(LogLevel::INFO)};
};

// Convenience macros
#define LOG_TRACE(comp, ...) ::orbit::Logger::trace(comp, __VA_ARGS__)
#define LOG_DEBUG(comp, ...) ::orbit::Logger::debug(comp, __VA_ARGS__)
#define LOG_INFO(comp, ...) ::orbit::Logger::info(comp, __VA_ARGS__)
#define LOG_WARN(comp, ...) ::orbit::Logger::warn(comp, __VA_ARGS__)
#define LOG_ERROR(comp, ...) ::orbit::Logger::error(comp, __VA_ARGS__)
#define LOG_FATAL(comp, ...) ::orbit::Logger::fatal(comp, __VA_ARGS__)

} // namespace orbit