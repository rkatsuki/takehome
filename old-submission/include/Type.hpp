#pragma once

#include <string>
#include <vector>
#include <list>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include <cstdint>
#include <compare>
#include <cstring>

#include "Constants.hpp"

// --- ID Types ---
using OrderID = uint64_t;
using ExecID  = uint64_t;
using SeqNum  = uint64_t;

// --- The Symbol Struct ---
// --- The "Zero-Copy" Symbol Struct ---
struct Symbol {
    char data[Config::SYMBOL_LENGTH] = {0};

    explicit Symbol(std::string_view n) {
        size_t len = std::min(n.size(), sizeof(data) - 1);
        std::memcpy(data, n.data(), len);
    }
    Symbol() = default;

    auto operator<=>(const Symbol& other) const = default;

    const char* c_str() const { return data; }
    // Updated empty check for the array
    bool empty() const { return data[0] == '\0'; }
    // Added for convenience in shell/printing
    std::string name() const { return std::string(data); }
};

// enable Symbol as hash key: hash specialization for the char array
namespace std {
    template<>
    struct hash<Symbol> {
        size_t operator()(const Symbol& s) const {
            // Hash the raw 12 bytes as a string_view
            return std::hash<std::string_view>{}(std::string_view(s.data, Config::SYMBOL_LENGTH));
        }
    };
}

// --- Enums & Basic Constants ---
enum class Side { BUY, SELL };
enum class OrderType { LIMIT, MARKET };
enum class OrderStatus { ACTIVE, FILLED, CANCELLED };

enum class EngineStatusCode {
    OK = 0,
    VALIDATION_FAILURE    = 400, // Updated to 400 for your Firewall Tests
    ORDER_ID_NOT_FOUND    = 102,
    SYMBOL_NOT_FOUND      = 103,
    TAG_NOT_FOUND         = 104,
    DUPLICATE_TAG         = 105,
    PRICE_OUT_OF_BAND     = 106,
    ALREADY_TERMINAL      = 107
};

// --- 1. OrderBook Internals ---
struct Order; 

struct OrderEntry {
    double remainingQuantity;
    std::shared_ptr<Order> fatOrder; 
};

struct PriceLevel {
    double price;
    double totalVolume = 0.0;
    std::list<OrderEntry> entries; 
};

struct OrderLocation {
    std::list<OrderEntry>::iterator it; // Iterator into the list is still stable
    double price;                       // Store the price to find the level later
    Side side;
};

// --- 2. The Order (The "Fat" Source of Truth) ---
struct Order {
    static inline std::atomic<OrderID> globalCounter{1000};
    
    // UPDATED: Standardized types
    const OrderID orderID = globalCounter.fetch_add(1, std::memory_order_relaxed);
    const uint64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count();

    double price;
    double originalQuantity;
    double remainingQuantity; 
    double cumulativeCost = 0.0; 

    Side side;
    OrderType type;
    OrderStatus status = OrderStatus::ACTIVE;
    
    Symbol symbol;   
    std::string tag;   

    mutable std::shared_mutex stateMutex; 

    Order(double p, double oQ, double rQ, double cC, Side s, 
          OrderType t, OrderStatus st, Symbol sym, std::string tg)
        : price(p), originalQuantity(oQ), remainingQuantity(rQ), 
          cumulativeCost(cC), side(s), type(t), status(st), 
          symbol(std::move(sym)), tag(std::move(tg)) {}

    Order(const Order&) = delete;
    Order& operator=(const Order&) = delete;

    auto operator<=>(const Order& other) const { return orderID <=> other.orderID; }
    bool operator==(const Order& other) const { return orderID == other.orderID; }

    [[nodiscard]] bool isFinished() const {
        std::shared_lock lock(stateMutex);
        return status != OrderStatus::ACTIVE;
    }
};

// --- 3. Snapshot & Messaging Types ---

struct BookLevel {
    double price;
    double quantity;
};

struct OrderBookSnapshot {
    Symbol symbol;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    SeqNum updateSeq = 0; // ADDED: For versioning
};

struct ShadowBuffer {
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    SeqNum sequence = 0;   // ADDED: For versioning
};

struct FillRecord {
    ExecID executionId;    // UPDATED
    double price;
    double quantity;
    OrderID takerOrderId;  // UPDATED
    OrderID makerOrderId;  // UPDATED
};

struct MatchResult {
    OrderID takerOrderId;  // UPDATED
    double remainingQuantity;
    std::vector<FillRecord> fills;
};

// --- 4. Engine Communication ---

struct EngineResponse {
    EngineStatusCode code;
    std::string message;
    std::shared_ptr<Order> order = nullptr;
    std::optional<OrderBookSnapshot> snapshot = std::nullopt;

    static EngineResponse Success(std::string msg, std::shared_ptr<Order> o = nullptr) {
        return { EngineStatusCode::OK, std::move(msg), std::move(o) };
    }
    static EngineResponse Error(EngineStatusCode c, std::string msg) {
        return { c, std::move(msg), nullptr };
    }
    
    bool isSuccess() const { return code == EngineStatusCode::OK; }
};

struct LimitOrderRequest { double price; double quantity; Side side; Symbol symbol; std::string tag; };
struct MarketOrderRequest { double quantity; Side side; Symbol symbol; std::string tag; };