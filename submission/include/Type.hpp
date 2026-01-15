#pragma once
#include <vector>
#include <string>
#include <variant>
#include <optional>
#include <cstdint>
#include "Constants.hpp"

// ====================================================================
// Core Enums
// ====================================================================
enum class Side : char { BUY = 'B', SELL = 'S' };
enum class OrderType : char { LIMIT = 'L', MARKET = 'M', CANCEL = 'C' };

// ====================================================================
// Order Entities
// ====================================================================

struct Order {
    long orderID;        
    std::string tag;     
    std::string symbol;
    Side side;
    OrderType type;
    double quantity;          // The original quantity requested
    double price;

    double remainingQuantity; // Current unfilled quantity (used by OrderBook)
    int64_t timestamp; // Nanoseconds since epoch

    // Helper for unit tests and registry
    bool isFilled() const { return remainingQuantity < Precision::EPSILON; }
};

// ====================================================================
// Engine Outputs (Snapshots & Trades)
// ====================================================================

// A single price level for the getOrderBook method
struct PriceLevel {
    double price;
    double size;
};

// The structured response for getOrderBook(symbol, depth)
struct OrderBookSnapshot {
    std::string symbol;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    double lastPrice;
    int64_t timestamp;     // nanoseconds
};

// A trade execution (was getOrderBook in snippet)
struct Execution {
    long executionID;
    long aggressorOrderID;
    long restingOrderID;
    
    // Helps with "Side" clarity
    Side aggressorSide; 
    
    std::string symbol;
    double price;
    double quantity;
    
    std::string buyTag;
    std::string sellTag;
    
    int64_t timestamp; // Nanoseconds since epoch
};

// ====================================================================
// Order Requests / User Intents
// ====================================================================

struct MarketOrderRequest {
    std::string tag;
    std::string symbol;
    Side side;
    double quantity;
};

struct LimitOrderRequest {
    std::string tag;
    std::string symbol;
    Side side;
    double quantity;
    double price;
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
        Order
    > data;

    bool isSuccess() const { return statusCode == 0; }
};
