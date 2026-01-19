#pragma once

#include <string_view>
#include <format>
#include <charconv>
#include <array>
#include <memory>

#include "Constants.hpp"
#include "ThreadSafeQueue.hpp"

enum class MsgType { 
    Data,   // For Kraken CSV responses (stdout)
    Error   // For Diagnostic/Error logs (stderr)
};

/**
 * @brief Zero-Allocation Message Carrier
 */
struct OutputEnvelope {
    std::array<char, 128> buffer; 
    size_t length{0};
    MsgType type;

    OutputEnvelope() = default;
    OutputEnvelope(MsgType t) noexcept : type(t) { buffer.fill(0); }
};

/**
 * @brief Stack-allocated numeric buffer to avoid heap usage in the hot path.
 */
struct SmartNum {
    std::array<char, 32> data;
    std::string_view view;
};

class OutputHandler {
private:
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> queue_;

    /**
     * @brief Zero-allocation Smart Formatter
     * @details Uses std::to_chars for fixed-point 8-decimal precision.
     * Strips trailing zeros so 100.00000000 becomes "100" and 
     * 100.00000001 stays "100.00000001".
     */
    SmartNum formatSmart(double value) noexcept {
        SmartNum out;
        // Use fixed format with 8 decimals to satisfy Satoshi precision (Scenario 8)
        auto [ptr, ec] = std::to_chars(out.data.data(), out.data.data() + out.data.size(), 
                                       value, std::chars_format::fixed, 8);
        
        if (ec != std::errc()) {
            out.view = "0";
            return out;
        }

        char* start = out.data.data();
        char* end = ptr - 1;

        // Strip trailing zeros
        while (end > start && *end == '0') {
            end--;
        }

        // Strip trailing decimal point if it's now the last character
        if (end > start && *end == '.') {
            end--;
        }

        out.view = std::string_view(start, end - start + 1);
        return out;
    }

    template <typename... Args>
    void enqueue(MsgType type, std::format_string<Args...> fmt, Args&&... args) noexcept {
        OutputEnvelope env(type);
        
        auto result = std::format_to_n(env.buffer.data(), 
                                       env.buffer.size() - 1, 
                                       fmt, 
                                       std::forward<Args>(args)...);
        
        env.length = result.size;
        env.buffer[env.length] = '\0'; 
        
        queue_->push(std::move(env));
    }

public:
    explicit OutputHandler(std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> queue) 
        : queue_(std::move(queue)) {}

    // --- CSV Outputs (The Execution Hot Path) ---

    void printAck(int uId, int uOid) noexcept {
        enqueue(MsgType::Data, "A, {}, {}\n", uId, uOid);
    }

    void printReject(int uId, int uOid, std::string_view reason) noexcept {
        // Enforce quotes around reason as required by test expectations for Scenario 14/16
        enqueue(MsgType::Data, "R, {}, {}, \"{}\"\n", uId, uOid, reason);
    }

    void printCancel(int uId, int uOid) noexcept {
        enqueue(MsgType::Data, "C, {}, {}\n", uId, uOid);
    }

    void printTrade(int bId, int bOid, int sId, int sOid, double p, double q) noexcept {
        // Use formatSmart().view to ensure clean integers or high-precision decimals
        enqueue(MsgType::Data, "T, {}, {}, {}, {}, {}, {}\n", 
                bId, bOid, sId, sOid, formatSmart(p).view, formatSmart(q).view);
    }

    void printBBO(char side, double p, double q) noexcept {
        if (q <= 1e-9) [[unlikely]] {
            enqueue(MsgType::Data, "B, {}, -, -\n", side);
        } else [[likely]] {
            enqueue(MsgType::Data, "B, {}, {}, {}\n", side, formatSmart(p).view, formatSmart(q).view);
        }
    }

    // --- Diagnostics (The Cold Path) ---

    void logError(std::string_view err) noexcept {
        if (Config::DEBUG) {
            enqueue(MsgType::Error, "[ERROR] {}\n", err);
        }
    }

    void logInfo(std::string_view info) noexcept {
        if (Config::DEBUG) {
            enqueue(MsgType::Error, "[INFO] {}\n", info);
        }
    }
};