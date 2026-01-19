#pragma once

#include <string_view>
#include <format>
#include <charconv>
#include <array>

#include "Constants.hpp"
#include "ThreadSafeQueue.hpp"

// EXPECTED OUTPUT 
// **Order acknowledgement:**
// A, userId, userOrderId
// **Cancel acknowledgement:**
// C, userId, userOrderId
// **Trade (matched orders):**
// T, userIdBuy, userOrderIdBuy, userIdSell, userOrderIdSell, price, quantity
// **Top of Order / BBO
// B, side (B or S), price, totalQuantity

/**
 * @brief Output Category
 * @details Separating Data from Errors allows the OutputThread to route
 * messages to different streams (stdout vs stderr) without the Engine
 * needing to know about file descriptors.
 */
enum class MsgType { 
    Data,   // For Kraken CSV responses (stdout)
    Error   // For Diagnostic/Error logs (stderr)
};

/**
 * @brief Zero-Allocation Message Carrier
 * @details This struct is specifically designed to be "Trivially Copyable."
 * By using a fixed-size std::array instead of std::string, we ensure that
 * pushing to the ThreadSafeQueue involves a single contiguous memory copy 
 * rather than a heap allocation and pointer indirection.
 */
struct OutputEnvelope {
    // 128 bytes fits most Kraken messages and stays within 2 CPU cache lines.
    std::array<char, 128> buffer; 
    size_t length{0};
    MsgType type;

    OutputEnvelope() = default;
    OutputEnvelope(MsgType t) noexcept : type(t) { buffer.fill(0); }
};

/**
 * @brief The Asynchronous Output Gateway
 * @details This class acts as a proxy. It formats data into a binary 
 * OutputEnvelope and hands it off to a background thread for printing.
 */
class OutputHandler {
private:
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> queue_;
    /**
     * @brief THE HOT-PATH FORMATTER
     * @details This is the "Secret Sauce" of the zero-allocation strategy.
     * 1. It creates an 'env' on the STACK.
     * 2. std::format_to_n writes the string directly into that stack memory.
     * 3. std::move(env) pushes that block of memory into the queue.
     * @note noexcept is critical here; since there is no 'new', it cannot throw bad_alloc.
     */
    template <typename... Args>
    void enqueue(MsgType type, std::format_string<Args...> fmt, Args&&... args) noexcept {
        OutputEnvelope env(type);
        
        // std::format_to_n is the high-performance variant of std::format.
        // It provides the safety of snprintf with the modern syntax of C++20.
        auto result = std::format_to_n(env.buffer.data(), 
                                       env.buffer.size() - 1, 
                                       fmt, 
                                       std::forward<Args>(args)...);
        
        env.length = result.size;
        
        // Safety: Ensure null termination for compatibility with C-style logging if needed.
        env.buffer[env.length] = '\0'; 
        
        // Move the stack-allocated struct into the thread-safe queue.
        queue_->push(std::move(env));
    }

    public:
        explicit OutputHandler(std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> queue) : queue_(std::move(queue)) {}

        // --- CSV Outputs (The Execution Hot Path) ---

    // Hot-Path Output Methods
    void printAck(int uId, int uOid) noexcept {
        // Exact match: A, 1, 101
        enqueue(MsgType::Data, "A, {}, {}\n", uId, uOid);
    }

    void printCancel(int uId, int uOid) noexcept {
        // Exact match: C, 1, 101
        enqueue(MsgType::Data, "C, {}, {}\n", uId, uOid);
    }

    void printTrade(int bId, int bOid, int sId, int sOid, double p, double q) noexcept {
        // Exact match: T, 1, 3, 2, 102, 11, 100
        // {:g} handles significant decimals automatically.
        enqueue(MsgType::Data, "T, {}, {}, {}, {}, {:g}, {:g}\n", bId, bOid, sId, sOid, p, q);
    }

    void printBBO(char side, double p, double q) noexcept {
        // Exact match: B, B, 10, 100  OR  B, S, -, -
        if (q <= 1e-9) [[unlikely]] {
            enqueue(MsgType::Data, "B, {}, -, -\n", side);
        } else [[likely]] {
            enqueue(MsgType::Data, "B, {}, {:g}, {:g}\n", side, p, q);
        }
    }
    // --- Diagnostics (The Cold Path) ---

    /**
     * @brief Error Logging
     * @details Used for rejections or system warnings. Routed to stderr.
     */
    void logError(std::string_view err) noexcept {
        if (Config::DEBUG){
            enqueue(MsgType::Error, "[ERROR] {}\n", err);
        }
    }
};