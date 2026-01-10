#pragma once

#include <map>
#include <unordered_map>
#include <vector>
#include <functional>
#include <shared_mutex>
#include <optional>
#include <string>
#include <mutex>
#include <algorithm>

#include "Order.hpp"

struct BookTop {
    double bestBid = 0.0;
    double bestAsk = 0.0;
    size_t bidLevels = 0;
    size_t askLevels = 0;
};

class OrderBook {
private:
    mutable std::shared_mutex bookMutex_;
    
    // Price-Time Priority: Bids (High to Low), Asks (Low to High)
    std::map<double, std::vector<long>, std::greater<double>> bids_;
    std::map<double, std::vector<long>> asks_; // Default is std::less
    
    // Core data storage: active orders currently resting in the book
    std::unordered_map<long, OrderEntry> orders_;
    
    // Trade history for this book
    std::vector<Execution> executions_;
    long nextExecId_ = 1;
    
    // Callback to notify the Engine when an order leaves the book (Filled/Cancelled)
    using CompletionCallback = std::function<void(long, const std::string&, const OrderEntry&)>;
    CompletionCallback onComplete_;

public:
    OrderBook() = default;

    // We now use the concrete 'Order' type instead of template 'T'
    void setCallback(CompletionCallback cb);
    void processOrder(const Order& order);
    
    std::optional<OrderEntry> getOrderDetails(long id) const;
    std::vector<Execution> flushExecutions();
    BookTop getTop() const;
};