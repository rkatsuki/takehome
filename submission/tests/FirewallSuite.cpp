#include <gtest/gtest.h>
#include "TradingEngine.hpp"
#include "Constants.hpp"

class FirewallSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(FirewallSuite, ValidationExhaustion) {
    // 1. Symbol Validation
    Order o1{1, "T1", "INVALID", Side::BUY, OrderType::LIMIT, 100.0, 10};
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
}

TEST_F(FirewallSuite, PriceBandingInitialState) {
    // Ensure that if no trades have happened, a far-away price is still accepted
    // (Market Opening / Price Discovery mode)
    Order o_far{1, "T1", "BTC/USD", Side::BUY, OrderType::LIMIT, 1000000.0, 1};
    EXPECT_EQ(engine.submitOrder(o_far).statusCode, 0); 
}