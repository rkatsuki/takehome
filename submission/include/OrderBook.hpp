#pragma once
#include <map>
#include <list>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

#include "OrderRegistry.hpp"

class OrderBook {
public:
struct PriceBucket {
        double totalVolume = 0.0;
        // deque becuase orders accumulates in the order (first-in) and are executed in that order (first-out)
        std::deque<Order> entries; 
    };
    explicit OrderBook(std::string sym);

    void execute(Order& incoming, std::vector<Execution>& globalExecs, std::mutex& execMutex, std::atomic<long>& nextExecId);
    bool cancelByTag(const std::string& tag);
    bool cancelById(long orderId);
    std::optional<Order> getActiveOrder(long orderId);
    std::optional<Order> getActiveOrderByTag(const std::string& tag);
    OrderBookSnapshot getSnapshot(int depth) const;

    double getLastPrice() const;
    void updateLastPrice(double price);

private:    
    std::string symbol;
    mutable std::mutex bookMutex;
    alignas(64) std::atomic<double> lastPrice{0.0};
    OrderRegistry registry;    
    std::map<double, PriceBucket, std::greater<double>> bids;
    std::map<double, PriceBucket, std::less<double>> asks;

    Execution generateExecution(const Order& aggressor, const Order& maker, 
                                        double qty, long execId, double matchPrice);
    // Template definition in header to ensure visibility for instantiation
    // Results in Static Dispatch, and superior for performance, vs lambda implementation
    template <typename T>
    void matchAgainstSide(Order& incoming, T& targets, std::vector<Execution>& localExecs, std::atomic<long>& nextExecId);
};