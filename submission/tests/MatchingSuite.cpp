#include <gtest/gtest.h>
#include "TradingEngine.hpp"

class MatchingSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(MatchingSuite, PriceLevelAndMemoryCleanup) {
    // 1. Place three orders at the same price
    Order o1{1, "T1", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10};
    Order o2{2, "T2", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10};
    Order o3{3, "T3", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10};

    engine.submitOrder(o1);
    engine.submitOrder(o2);
    engine.submitOrder(o3);

    // 2. Sweep the level completely
    // This exercises the 'while' loop and the final map->erase() cleanup
    engine.submitOrder({4, "T4", "BTC/USD", Side::SELL, OrderType::LIMIT, 100.0, 30});

    // 3. Verify Registry is clean
    EXPECT_FALSE(engine.getActiveOrderById(1).isSuccess());
    EXPECT_FALSE(engine.getActiveOrderById(2).isSuccess());
    EXPECT_FALSE(engine.getActiveOrderById(3).isSuccess());
    
    // 4. Price Improvement Check
    engine.submitOrder({5, "T5", "BTC/USD", Side::SELL, OrderType::LIMIT, 90.0, 10});
    auto res = engine.submitOrder({6, "T6", "BTC/USD", Side::BUY, OrderType::LIMIT, 110.0, 5});
    // The execution should happen at 90.0 (resting price), not 110.0
    // (Verify this via your ExecutionReport/Event system if implemented)
}

TEST_F(MatchingSuite, MarketOrderExpiration) {
    // Market order hitting an empty book should not rest
    Order mkt{1, "T1", "BTC/USD", Side::BUY, OrderType::MARKET, 0.0, 10};
    auto res = engine.submitOrder(mkt);
    EXPECT_FALSE(engine.getActiveOrderById(1).isSuccess()); // Should not be in registry
}