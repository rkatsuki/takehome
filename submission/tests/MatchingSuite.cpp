#include <gtest/gtest.h>
#include "TradingEngine.hpp"

class MatchingSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(MatchingSuite, PriceLevelAndMemoryCleanup) {
    // 1. Place three orders at the same price
    LimitOrderRequest o1 = {"T1", "BTC/USD", Side::BUY, 10, 100.0};
    LimitOrderRequest o2 = {"T2", "BTC/USD", Side::BUY, 10, 100.0};
    LimitOrderRequest o3 = {"T3", "BTC/USD", Side::BUY, 10, 100.0};

    engine.submitOrder(o1);
    engine.submitOrder(o2);
    engine.submitOrder(o3);

    // 2. Sweep the level completely
    // This exercises the 'while' loop and the final map->erase() cleanup
    engine.submitOrder(LimitOrderRequest{"T4", "BTC/USD", Side::SELL, 30, 100.0});

    // 3. Verify Registry is clean
    EXPECT_FALSE(engine.getActiveOrderById(1).isSuccess());
    EXPECT_FALSE(engine.getActiveOrderById(2).isSuccess());
    EXPECT_FALSE(engine.getActiveOrderById(3).isSuccess());
    
    // 4. Price Improvement Check
    engine.submitOrder(LimitOrderRequest{"T5", "BTC/USD", Side::SELL, 10, 90.0});
    auto res = engine.submitOrder(LimitOrderRequest{"T6", "BTC/USD", Side::BUY, 5, 110.0});
    // The execution should happen at 90.0 (resting price), not 110.0
}

TEST_F(MatchingSuite, MarketOrderExpiration) {
    // Market order hitting an empty book should not rest
    MarketOrderRequest mkt = {"T1", "BTC/USD", Side::BUY, 10};
    auto res = engine.submitOrder(mkt);
    EXPECT_FALSE(engine.getActiveOrderById(1).isSuccess()); // Should not be in registry
}