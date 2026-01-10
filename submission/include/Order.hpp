#pragma once

#include <concepts>
#include <vector>
#include <string>

#include "Constants.hpp"

// ====================================================================
// C++20 Order Concept
// ====================================================================

struct OrderEntry {
    double price;
    long remainingQuantity;
    long originalQuantity;
    Side side;
    std::string tag;
};

struct OrderLocation {
    long orderID;        
    std::string symbol;  
    std::string tag;
};

struct Execution {
    long executionID;
    long aggressorOrderID;
    long restingOrderID;
    double price;
    long quantity;
};

struct Order {
    long orderID;
    std::string tag;
    std::string symbol;
    Side side;
    OrderType type;
    double price;
    long quantity;
};