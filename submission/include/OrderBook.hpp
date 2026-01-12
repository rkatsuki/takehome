#pragma once
#include <map>
#include <list>
#include <mutex>
#include <atomic>
#include <vector>
#include <string>

#include "OrderRegistry.hpp"

class OrderBook {
public:
    struct PriceBucket {
        long totalVolume = 0;
        std::list<OrderEntry> entries;
    };

    explicit OrderBook(std::string sym);

    void execute(Order& incoming, std::vector<Execution>& globalExecs, std::mutex& execMutex, std::atomic<long>& nextExecId);
    bool cancelByTag(const std::string& tag);
    bool cancelById(long orderId);
    std::optional<OrderEntry> getActiveOrder(long orderId);
    std::optional<OrderEntry> getActiveOrderByTag(const std::string& tag);
    OrderBookSnapshot getSnapshot(int depth) const;
    double getLastPrice() const;
    void updateLastPrice(double price);

private:
    std::atomic<double> lastPrice{0.0}; // Initialized to 0.0 for the "Initial State"
    std::string symbol;
    mutable std::mutex bookMutex;
    OrderRegistry registry;
    
    std::map<double, PriceBucket, std::greater<double>> bids;
    std::map<double, PriceBucket, std::less<double>> asks;

    // Template definition in header to ensure visibility for instantiation
    // Results in Static Dispatch, and superior for performance, vs lambda implementation
    template <typename T>
    void matchAgainstSide(Order& incoming, T& targets, std::vector<Execution>& localExecs, std::atomic<long>& nextExecId);
};