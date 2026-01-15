#include <gtest/gtest.h>
#include "TradingEngine.hpp"
#include "Constants.hpp"

class FirewallSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(FirewallSuite, ValidationExhaustion) {
    // 1. Symbol Validation
    LimitOrderRequest o1 = {"T1", "INVALID_SYMBOL", Side::BUY, 10, 100.0};
    EXPECT_EQ(engine.submitOrder(o1).statusCode, 400);

    // 2. Quantity Boundaries (Min/Max/Zero)
    o1.symbol = "BTC/USD";
    o1.quantity = 0;
    EXPECT_EQ(engine.submitOrder(o1).statusCode, 400);
    o1.quantity = Config::MAX_ORDER_QTY + 1;
    EXPECT_EQ(engine.submitOrder(o1).statusCode, 400);

    // 3. Price Boundaries
    o1.quantity = 10;
    o1.price = -1.0;
    EXPECT_EQ(engine.submitOrder(o1).statusCode, 400);

    // 4. Tag Length Check
    o1.price = 100.0;
    o1.tag = std::string(Config::MAX_TAG_SIZE + 1, 'A');
    EXPECT_EQ(engine.submitOrder(o1).statusCode, 400);

    // 4. All corrected and expect success
    o1.tag = std::string(Config::MAX_TAG_SIZE, 'A');
    EXPECT_TRUE(engine.submitOrder(o1).isSuccess());
}

TEST_F(FirewallSuite, PriceBandingInitialState) {
    // Ensure that if no trades have happened, a far-away price is still accepted
    // (Market Opening / Price Discovery mode)
    LimitOrderRequest o_far = {"T1", "BTC/USD", Side::BUY, 1, 1000000.0};
    EXPECT_EQ(engine.submitOrder(o_far).statusCode, 0); 
}

TEST_F(FirewallSuite, AggressivePriceBanding) {
    // 1. Establish a last price of 100.0 by matching two orders
    engine.submitOrder(LimitOrderRequest{"T1", "BTC/USD", Side::BUY, 10, 100.0});
    engine.submitOrder(LimitOrderRequest{"T2", "BTC/USD", Side::SELL, 10, 100.0});

    // 2. Try to buy at 120.0 (Assuming a 10% band limit)
    LimitOrderRequest fatFinger = {"OOPS", "BTC/USD", Side::BUY, 10, 1000.0};
    auto res = engine.submitOrder(fatFinger);

    EXPECT_FALSE(res.isSuccess());
    EXPECT_EQ(res.message, "Price outside banding limits");
}

TEST_F(FirewallSuite, MaxQuantityViolation) {
    // Assuming Config::MAX_ORDER_QTY = 1,000,000
    LimitOrderRequest bigOrder = {"BIG", "BTC/USD", Side::BUY, Config::MAX_ORDER_QTY+1, 50000.0};
    auto res = engine.submitOrder(bigOrder);

    EXPECT_FALSE(res.isSuccess());
    EXPECT_EQ(res.statusCode, 400); 
    EXPECT_TRUE(res.message.find("quantity") != std::string::npos);
}

TEST_F(FirewallSuite, TickSizeValidation) {
    // Assuming Tick Size is 0.01
    LimitOrderRequest badTick = {"TICK", "BTC/USD", Side::BUY, 10, Config::MAX_ORDER_PRICE+1};
    auto res = engine.submitOrder(badTick);

    EXPECT_FALSE(res.isSuccess());
    EXPECT_TRUE(res.message.find("price") != std::string::npos);
}