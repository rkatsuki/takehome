#pragma once
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include "Constants.hpp"

// ====================================================================
// Core Enums
// ====================================================================
enum class Side : char { BUY = 'B', SELL = 'S' };
enum class OrderType : char { LIMIT = 'L', MARKET = 'M', CANCEL = 'C' };

// ====================================================================
// Order Entities
// ====================================================================

// The raw input from the user/network
struct Order {
    long orderID;        // Internal unique ID
    std::string tag;     // User-provided string ID
    std::string symbol;
    Side side;
    OrderType type;
    double price;
    long quantity;
};

// Represents an order sitting inside the OrderBook's internal lists
struct OrderEntry {
    double price;
    long remainingQuantity;
    long originalQuantity;
    Side side;
    std::string tag;
    long orderID;
};

// ====================================================================
// Engine Outputs (Snapshots & Trades)
// ====================================================================

// A single price level for the getOrderBook method
struct PriceLevel {
    double price;
    long size;
};

// The structured response for getOrderBook(symbol, depth)
struct OrderBookSnapshot {
    std::string symbol;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

// A trade execution (was getOrderBook in your snippet)
struct Execution {
    long executionID;
    long aggressorOrderID;
    long restingOrderID;
    std::string symbol;
    double price;
    long quantity;
    std::string buyTag;
    std::string sellTag;
};

// ====================================================================
// Response Wrapper
// ====================================================================

struct OrderAcknowledgement {
    long orderID;
    std::string tag;
};

struct EngineResponse {
    int statusCode; // 0: Success, 1+: Error
    std::string message;
    
    // Using std::monostate for "void" success responses
    std::variant<
        std::monostate,
        OrderAcknowledgement,
        OrderBookSnapshot, 
        std::vector<Execution>, 
        OrderEntry
    > data;

    bool isSuccess() const { return statusCode == 0; }
};
