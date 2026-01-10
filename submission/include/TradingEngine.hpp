#pragma once

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <iostream>

#include "OrderBook.hpp"
#include "OrderRegistry.hpp"

class TradingEngine {
private:
    // Protects the symbolBooks_ map (adding/removing symbols)
    mutable std::shared_mutex engineMutex_;
    
    // Map of Symbol -> Specific Order Book (Templates removed)
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> symbolBooks_;
    
    OrderRegistry registry_;

    // Helper to get or create a book for a symbol
    OrderBook& getOrCreateBook(const std::string& symbol);

    // Internal callback handler for when an order is completed/removed from a book
    void handleOrderCompletion(long id, const std::string& tag, const OrderEntry& state);

public:
    TradingEngine() = default;

    // Core Trading Actions
    void executeOrder(Order& order);
    void executeCancelById(long orderId);
    void executeCancelByTag(const std::string& tag);   

    // Queries
    void printOrderStateByTag(const std::string& tag) const;
    void reportExecutions();
    void printOrderBookState(const std::string& symbol) const;

    // Delete copy/assignment for safety
    TradingEngine(const TradingEngine&) = delete;
    TradingEngine& operator=(const TradingEngine&) = delete;
};