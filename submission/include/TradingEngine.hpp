#pragma once

#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <optional>

#include "Type.hpp"
#include "OrderBook.hpp"

/**
 * @brief The TradingEngine: The Central Hub of the Matching System.
 */
class TradingEngine {
public:
    TradingEngine();

    // --- Order Ingress (Public API) ---
    EngineResponse submitOrder(const LimitOrderRequest& req);
    EngineResponse submitOrder(const MarketOrderRequest& req);

    // --- Query & Control (Public API) ---
    // Updated: Uses OrderID (uint64_t)
    EngineResponse getOrder(OrderID id);
    EngineResponse getOrderByTag(const std::string& tag);
    
    // Updated: Uses Symbol struct
    EngineResponse getOrderBookSnapshot(const Symbol& symbol, size_t depth);
    
    // Updated: Uses OrderID (uint64_t)
    EngineResponse cancelOrder(OrderID id);
    EngineResponse cancelOrderByTag(const std::string& tag);

private:
    // --- Internal Logic Pipeline ---
    
    // Updated: Uses Symbol and OrderID types
    EngineResponse validateCommon(const Symbol& symbol, double quantity, 
                                 std::optional<double> price, const std::string& tag);

    EngineResponse processOrder(std::shared_ptr<Order> order);

    EngineResponse finalizeExecution(const MatchResult& result, std::shared_ptr<Order> taker);

    EngineResponse internalCancel(OrderID orderId);

    // --- Venue Management ---
    
    // Updated: Uses Symbol struct
    OrderBook* getOrAddBook(const Symbol& sym);
    OrderBook* tryGetBook(const Symbol& sym) const;

    // --- Data Members ---

    // The Registry: Global map of all active and finished orders.
    // Updated: Keyed by OrderID (uint64_t)
    std::unordered_map<OrderID, std::shared_ptr<Order>> idRegistry;
    std::unordered_map<std::string, OrderID> tagToId;
    mutable std::shared_mutex registryMutex; 

    // The Bookshelf: Manages the collection of OrderBooks.
    // Updated: Keyed by Symbol struct (leveraging your custom std::hash<Symbol>)
    std::unordered_map<Symbol, std::unique_ptr<OrderBook>> symbolBooks;
    mutable std::shared_mutex bookshelfMutex; 

    // Global counters for the system
    // Updated: Uses ExecID (uint64_t)
    std::atomic<ExecID> nextExecId{1000000}; 
};