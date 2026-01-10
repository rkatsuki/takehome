#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <shared_mutex>
#include <list>
#include <mutex>
#include <iostream>

#include "Order.hpp"

class OrderRegistry {
private:
    // --- Active Orders Tracking ---
    mutable std::shared_mutex activeMutex_;
    std::unordered_map<long, std::string> idToSymbol_;
    std::unordered_map<std::string, long> tagToId_;
    std::unordered_map<std::string, std::string> tagToSymbol_;
    std::unordered_map<long, std::string> idToTag_;

    // --- History Tracking (Audit Trail) ---
    struct HistoricalOrder {
        std::string symbol;
        double price;
        long originalQty;
        std::string status; // "FILLED" or "CANCELLED"
    };

    mutable std::shared_mutex historyMutex_;
    std::unordered_map<std::string, HistoricalOrder> history_;
    std::list<std::string> historyOrderQueue_; 
    const size_t MAX_HISTORY = 1000;

public:
    OrderRegistry() = default;

    // Active Management
    void registerOrder(long id, const std::string& tag, const std::string& symbol);
    
    // Returns the location info so Engine knows which book to talk to
    std::optional<OrderLocation> unregisterById(long id);
    std::optional<OrderLocation> unregisterByTag(const std::string& tag);
    
    // Lookup methods
    std::optional<OrderLocation> getLocationByTag(const std::string& tag) const;
    std::optional<OrderLocation> getLocationById(long id) const;
    
    // History Management
    void recordHistory(const std::string& tag, const std::string& symbol, double price, long qty, const std::string& status);
    void printHistoryIfExists(const std::string& tag) const;
};