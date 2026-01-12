#pragma once
#include <unordered_map>
#include <shared_mutex>
#include <array>
#include <memory>

#include "Type.hpp"
#include "OrderBook.hpp"

class TradingEngine {
public:
    TradingEngine();
    EngineResponse submitOrder(const Order& order);
    EngineResponse cancelOrderByTag(const std::string& tag, const std::string& symbol);
    EngineResponse cancelOrderById(long orderId);
    EngineResponse getOrderBook(const std::string& symbol, int depth = 1);
    EngineResponse reportExecutions();

    // Queries for resting orders only
    EngineResponse getActiveOrderById(long orderId);
    EngineResponse getActiveOrderByTag(const std::string& tag, const std::string& symbol);

private:
    struct alignas(64) ResourceMonitor {
        std::atomic<long> currentGlobalOrderCount{0};
    };
    ResourceMonitor monitor;


    OrderBook* getBook(const std::string& symbol);

    mutable std::shared_mutex bookshelfMutex;
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> symbolBooks;

    static constexpr size_t ID_SHARD_COUNT = 16;
    struct IdShard {
        mutable std::shared_mutex mutex;
        std::unordered_map<long, std::string> mapping;
    };
    std::array<IdShard, ID_SHARD_COUNT> idShards;

    std::mutex execHistoryMutex;
    std::vector<Execution> executionHistory;

    std::atomic<long> nextOrderId{1};
    std::atomic<long> nextExecId{1};
};