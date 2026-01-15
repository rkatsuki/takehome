#include <gtest/gtest.h>
#include "TradingEngine.hpp"

class PrecisionSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

// This tests if ten small fills correctly add up to a whole, or if they leave a microscopic remainder that keeps a price level "alive."
TEST_F(PrecisionSuite, AccumulatedDustCleanup) {
    std::string symbol = "BTC/USD";
    double targetPrice = 50000.0;
    double buySize = 1.0;
    double sellSize = 0.1000000001;
    
    // 1. Place a 1.0 BTC Buy Order
    engine.submitOrder(LimitOrderRequest{"MAKER", symbol, Side::BUY, buySize, targetPrice});

    // 2. Fill it with 10 small Sell orders of 0.1000000001 
    // The extra 0.0000000001 ensures the maker is definitely exhausted
    for(int i = 0; i < 10; ++i) {
        engine.submitOrder(LimitOrderRequest{"TAKER", symbol, Side::SELL, sellSize, targetPrice});
    }

    // 3. Verify the Price Level is physically removed from the map
    auto snap = std::get<OrderBookSnapshot>(engine.getOrderBook(symbol, 5).data);
    
    for (const auto& level : snap.bids) {
        EXPECT_NE(level.price, targetPrice) << "Price level should be erased, but found residual volume: " << level.size;
    }
    EXPECT_EQ(snap.lastPrice, targetPrice);

    // // 4. Verify that if one more sell of the same can correctly be reflected in the Orderbook
    engine.submitOrder(LimitOrderRequest{"TAKER", symbol, Side::SELL, sellSize, targetPrice});
    auto tob = std::get<OrderBookSnapshot>(engine.getOrderBook(symbol, 5).data);
    EXPECT_EQ(tob.asks[0].price, targetPrice);
    EXPECT_EQ(tob.asks[0].size, sellSize);
    // last price should not have changed.
    EXPECT_EQ(tob.lastPrice, targetPrice);
}

// This ensures that if an order's remainingQuantity falls below our threshold (1e-10), the OrderRegistry and PriceBucket are cleaned up immediately rather than waiting for another trade.
TEST_F(PrecisionSuite, SubEpsilonExhaustion) {
    // Submit a Buy for 1.0
    engine.submitOrder(LimitOrderRequest{"B1", "BTC/USD", Side::BUY, 1.0, 100.0});

    // Match it with a Sell for 0.999999999999 (well within Epsilon of 1.0)
    // This leaves 0.000000000001 remaining.
    engine.submitOrder(LimitOrderRequest{"S1", "BTC/USD", Side::SELL, 0.999999999999, 100.0});

    // 1. Registry check: B1 should be gone
    auto getRes = engine.getActiveOrderByTag("B1", "BTC/USD");
    EXPECT_FALSE(getRes.isSuccess()) << "Order should be removed from registry when remaining volume is sub-epsilon";

    // 2. Snapshot check: Volume should be 0 or level erased
    auto snap = std::get<OrderBookSnapshot>(engine.getOrderBook("BTC/USD", 1).data);
    EXPECT_TRUE(snap.bids.empty());
}

// totalVolume at a price level is tracked separately from individual order quantities for O(1) snapshot generation. This test ensures they don't drift apart.
TEST_F(PrecisionSuite, VolumeDriftPrevention) {
    // 1. Create a deep level with many small orders
    for(int i = 0; i < 100; ++i) {
        engine.submitOrder(LimitOrderRequest{"T" + std::to_string(i), "BTC/USD", Side::BUY, 0.00012345, 1000.0});
    }

    // 2. Cancel half of them
    for(int i = 0; i < 50; ++i) {
        engine.cancelOrderByTag("T" + std::to_string(i), "BTC/USD");
    }

    // 3. Verify snapshot volume matches the sum of remaining orders exactly
    auto snap = std::get<OrderBookSnapshot>(engine.getOrderBook("BTC/USD", 1).data);
    double expectedVol = 50 * 0.00012345;
    
    ASSERT_FALSE(snap.bids.empty());
    EXPECT_NEAR(snap.bids[0].size, expectedVol, 1e-12);
}