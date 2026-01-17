#pragma once

#include <iostream>
#include <string_view>
#include <charconv>
#include <queue>
#include <mutex>
#include <optional>
#include <atomic>
#include <iomanip>
#include <format>
#include <condition_variable>
#include "TradingEngine.hpp"

// --- Thread-Safe Blocking Queue ---
template<typename T>
class ThreadSafeQueue {
private:
    std::queue<T> queue;
    mutable std::mutex mutex;
    std::condition_variable cv;

public:
    /**
     * Pushes an item and notifies the waiting listener thread.
     */
    void push(T value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(std::move(value));
        }
        cv.notify_one();
    }

    /**
     * Blocks the calling thread until an item is available.
     */
    T wait_and_pop() {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !queue.empty(); });
        T value = std::move(queue.front());
        queue.pop();
        return value;
    }

    /**
     * Non-blocking attempt to pull an item. Returns std::nullopt if empty.
     */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return std::nullopt;
        T value = std::move(queue.front());
        queue.pop();
        return value;
    }

    /**
     * Returns true if the queue is empty. Used for final shutdown drain.
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
};

// --- High-Performance Zero-Copy Utilities ---

/**
 * Advanced string_view window to extract tokens without allocation.
 */
inline std::string_view get_next_token(std::string_view& input) {
    auto start = input.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return "";
    input.remove_prefix(start);
    
    auto end = input.find_first_of(" \t\r\n");
    std::string_view token = input.substr(0, end);
    input.remove_prefix(end == std::string_view::npos ? input.size() : end);
    return token;
}

/**
 * Fast integer conversion using <charconv>.
 */
template<typename T>
inline T to_num(std::string_view sv) {
    T val{};
    if (sv.empty()) return val;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

/**
 * Fast double conversion using <charconv>.
 */
inline double to_double(std::string_view sv) {
    double val = 0.0;
    if (sv.empty()) return val;
    // Note: ensure your compiler fully supports floating point from_chars (C++20)
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

// --- UI/Display Prototypes ---

/**
 * Formats and prints specific order details.
 */
void displayOrderReport(const Order& o);

/**
 * Renders the OrderBook bids/asks with ANSI colors.
 */
void displayBook(const OrderBookSnapshot& snap);

/**
 * Central dispatcher for engine responses.
 */
void handleResponse(const EngineResponse& resp);