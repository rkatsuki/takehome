#include <gtest/gtest.h>
#include <memory>
#include <cstring>
#include "OrderBook.hpp"

class OrderBookTestSuite : public ::testing::Test {
protected:
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> testQueue;
    OutputHandler handler;
    Symbol btc;
    OrderBook book;
    std::unordered_map<OrderKey, OrderLocation> registry;

    OrderBookTestSuite() 
        : testQueue(std::make_shared<ThreadSafeQueue<OutputEnvelope>>()),
          handler(testQueue),
          book(btc, handler) 
    {
        memset(btc.data, 0, 8);
        memcpy(btc.data, "BTC", 3);
    }

    // --- Helper Methods ---
    Command createOrder(uint32_t uid, uint32_t oid, Side side, double price, double qty) {
        Command cmd{};
        cmd.type = CommandType::NEW;
        cmd.side = side;
        cmd.userId = uid;
        cmd.userOrderId = oid;
        cmd.price = price;
        cmd.quantity = qty;
        memcpy(cmd.symbol.data, btc.data, 8);
        return cmd;
    }

    double getVolume(double price, Side side) {
        if (side == Side::BUY) {
            return book.bids_.contains(price) ? book.bids_.at(price).totalVolume : 0.0;
        } else {
            return book.asks_.contains(price) ? book.asks_.at(price).totalVolume : 0.0;
        }
    }

    size_t getLevelCount(Side side) const {
        return (side == Side::BUY) ? book.bids_.size() : book.asks_.size();
    }
};

// --- PAIR 1: Basic Placement ---

TEST_F(OrderBookTestSuite, Placement_BidSide) {
    auto cmd = createOrder(1, 101, Side::BUY, 100.0, 10.0);
    book.execute(cmd, registry); // Passed as lvalue
    EXPECT_EQ(getLevelCount(Side::BUY), 1);
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::BUY), 10.0);
}

TEST_F(OrderBookTestSuite, Placement_AskSide) {
    auto cmd = createOrder(1, 101, Side::SELL, 100.0, 10.0);
    book.execute(cmd, registry);
    EXPECT_EQ(getLevelCount(Side::SELL), 1);
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::SELL), 10.0);
}

// --- PAIR 2: Price Improvement ---



TEST_F(OrderBookTestSuite, PriceImprovement_BuyTaker) {
    auto m = createOrder(1, 101, Side::SELL, 100.0, 10.0);
    auto t = createOrder(2, 202, Side::BUY, 110.0, 5.0);
    book.execute(m, registry);
    book.execute(t, registry);
    
    EXPECT_DOUBLE_EQ(book.getLastTradedPrice(), 100.0);
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::SELL), 5.0);
}

TEST_F(OrderBookTestSuite, PriceImprovement_SellTaker) {
    auto m = createOrder(1, 101, Side::BUY, 100.0, 10.0);
    auto t = createOrder(2, 202, Side::SELL, 90.0, 5.0);
    book.execute(m, registry);
    book.execute(t, registry);

    EXPECT_DOUBLE_EQ(book.getLastTradedPrice(), 100.0);
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::BUY), 5.0);
}

// --- PAIR 3: Multi-Level Sweeps ---



TEST_F(OrderBookTestSuite, Sweep_BuyTaker) {
    auto m1 = createOrder(1, 1, Side::SELL, 100.0, 10.0);
    auto m2 = createOrder(1, 2, Side::SELL, 101.0, 10.0);
    auto t = createOrder(2, 1, Side::BUY, 110.0, 15.0);
    
    book.execute(m1, registry);
    book.execute(m2, registry);
    book.execute(t, registry);
    
    EXPECT_EQ(getLevelCount(Side::SELL), 1);
    EXPECT_DOUBLE_EQ(getVolume(101.0, Side::SELL), 5.0);
}

TEST_F(OrderBookTestSuite, Sweep_SellTaker) {
    auto m1 = createOrder(1, 1, Side::BUY, 100.0, 10.0);
    auto m2 = createOrder(1, 2, Side::BUY, 99.0, 10.0);
    auto t = createOrder(2, 1, Side::SELL, 90.0, 15.0);
    
    book.execute(m1, registry);
    book.execute(m2, registry);
    book.execute(t, registry);
    
    EXPECT_EQ(getLevelCount(Side::BUY), 1);
    EXPECT_DOUBLE_EQ(getVolume(99.0, Side::BUY), 5.0);
}

// --- PAIR 4: Precision Boundaries ---

TEST_F(OrderBookTestSuite, EpsilonMatch_BuyTaker) {
    double m_price = 100.0;
    double t_price = 100.0 - (Precision::EPSILON / 2.0); 

    auto maker = createOrder(1, 1, Side::SELL, m_price, 10.0);
    auto taker = createOrder(2, 1, Side::BUY, t_price, 10.0);

    book.execute(maker, registry);
    book.execute(taker, registry);

    EXPECT_EQ(getLevelCount(Side::SELL), 0);
}

TEST_F(OrderBookTestSuite, EpsilonMatch_SellTaker) {
    double m_price = 100.0;
    double t_price = 100.0 + (Precision::EPSILON / 2.0); 

    auto maker = createOrder(1, 1, Side::BUY, m_price, 10.0);
    auto taker = createOrder(2, 1, Side::SELL, t_price, 10.0);

    book.execute(maker, registry);
    book.execute(taker, registry);

    EXPECT_EQ(getLevelCount(Side::BUY), 0);
}

/**
 * @test FIFO & Sequence Logic
 * Verifies that orders at the same price are matched in the order they arrived,
 * and price levels are consumed from best to worst.
 */
TEST_F(OrderBookTestSuite, FIFO_And_PriceSequence) {
    // 1. Place 3 orders at Price 100.0 (Testing Time Priority)
    auto m1 = createOrder(10, 1, Side::SELL, 100.0, 5.0); // Should be hit 1st
    auto m2 = createOrder(20, 2, Side::SELL, 100.0, 5.0); // Should be hit 2nd
    auto m3 = createOrder(30, 3, Side::SELL, 100.0, 5.0); // Should be hit 3rd
    
    book.execute(m1, registry);
    book.execute(m2, registry);
    book.execute(m3, registry);

    // 2. Place 1 order at Price 101.0 (Testing Price Priority)
    auto m4 = createOrder(40, 4, Side::SELL, 101.0, 5.0); // Should be hit last
    book.execute(m4, registry);

    // 3. Taker Buy 12 units at 110.0
    // This should eat: ALL of m1 (5), ALL of m2 (5), and 2 units of m3.
    // m4 should remain untouched.
    auto taker = createOrder(99, 99, Side::BUY, 110.0, 12.0);
    book.execute(taker, registry);

    // --- VERIFICATION ---
    
    // Check Registry: m1 and m2 should be gone. m3 and m4 should remain.
    EXPECT_FALSE(registry.contains({10, 1}));
    EXPECT_FALSE(registry.contains({20, 2}));
    EXPECT_TRUE(registry.contains({30, 3}));
    EXPECT_TRUE(registry.contains({40, 4}));

    // Check Volume:
    // m3 had 5.0, 2.0 were taken, 3.0 should remain at price 100.0
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::SELL), 3.0);
    // m4 (5.0) at 101.0 should be untouched
    EXPECT_DOUBLE_EQ(getVolume(101.0, Side::SELL), 5.0);
}

/**
 * @test FIFO Sell-Side (Bids)
 * Verifies that multiple resting Bids at the same price are matched 
 * in arrival order when hit by a large Taker Sell.
 */
TEST_F(OrderBookTestSuite, FIFOSymmetry_SellSide) {
    // 1. Place 3 Bids at Price 100.0 (Time Priority)
    auto b1 = createOrder(10, 1, Side::BUY, 100.0, 10.0); // 1st
    auto b2 = createOrder(20, 2, Side::BUY, 100.0, 10.0); // 2nd
    auto b3 = createOrder(30, 3, Side::BUY, 100.0, 10.0); // 3rd
    
    book.execute(b1, registry);
    book.execute(b2, registry);
    book.execute(b3, registry);

    // 2. Place 1 Bid at Price 99.0 (Price Priority)
    auto b4 = createOrder(40, 4, Side::BUY, 99.0, 10.0); // Best Price 100.0 must be hit first
    book.execute(b4, registry);

    // 3. Taker Sell 25 units at 90.0
    // This should gulp: ALL of b1 (10), ALL of b2 (10), and 5 units of b3.
    auto taker = createOrder(99, 99, Side::SELL, 90.0, 25.0);
    book.execute(taker, registry);

    // --- VERIFICATION ---
    
    // b1 and b2 should be erased from registry
    EXPECT_FALSE(registry.contains({10, 1}));
    EXPECT_FALSE(registry.contains({20, 2}));
    
    // b3 should still be there (partially filled)
    EXPECT_TRUE(registry.contains({30, 3}));
    
    // b4 should be untouched (worse price than the level being hit)
    EXPECT_TRUE(registry.contains({40, 4}));

    // Volume Checks
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::BUY), 5.0);  // 30.0 - 25.0
    EXPECT_DOUBLE_EQ(getVolume(99.0, Side::BUY), 10.0);  // Untouched
}

/**
 * @test Aggressive Gulp
 * A Taker selling at a very low price should still respect 
 * the descending price priority of the Bid book.
 */
TEST_F(OrderBookTestSuite, AggressiveGulp_SellSide) {
    // Two price levels
    auto b1 = createOrder(1, 1, Side::BUY, 105.0, 10.0); 
    auto b2 = createOrder(2, 2, Side::BUY, 100.0, 10.0);
    book.execute(b1, registry);
    book.execute(b2, registry);

    // Taker Sell at $1.0 (Very aggressive)
    auto taker = createOrder(3, 3, Side::SELL, 1.0, 15.0);
    book.execute(taker, registry);

    // Verify it took all of the BEST price (105) and half of the next (100)
    EXPECT_FALSE(registry.contains({1, 1}));
    EXPECT_DOUBLE_EQ(getVolume(100.0, Side::BUY), 5.0);
    EXPECT_DOUBLE_EQ(book.getLastTradedPrice(), 100.0); // Should be price of the last matched maker
}

/**
 * @test Stress: Thundering Herd
 * 1,000 makers at the same price, 1 taker clears them all.
 */
TEST_F(OrderBookTestSuite, Stress_ThunderingHerd) {
    const int orderCount = 1000;
    const double price = 100.0;
    
    // 1. Flood the book with 1,000 small Sell orders
    for (int i = 0; i < orderCount; ++i) {
        auto m = createOrder(i, i, Side::SELL, price, 1.0);
        book.execute(m, registry);
    }
    
    ASSERT_EQ(getLevelCount(Side::SELL), 1);
    ASSERT_DOUBLE_EQ(getVolume(price, Side::SELL), (double)orderCount);

    // 2. One massive Buy Taker to clear the level
    auto t = createOrder(9999, 1, Side::BUY, price, (double)orderCount);
    book.execute(t, registry);

    // 3. Verify the level is purged and registry is clean
    EXPECT_EQ(getLevelCount(Side::SELL), 0);
    EXPECT_EQ(registry.size(), 0);
}

/**
 * @test Stress: Ladder Sweep
 * Sweeping across 500 different price levels.
 */
TEST_F(OrderBookTestSuite, Stress_LadderSweep) {
    const int levels = 500;
    
    // 1. Create a ladder of Sells from 100 to 599
    for (int i = 0; i < levels; ++i) {
        auto m = createOrder(i, i, Side::SELL, 100.0 + i, 1.0);
        book.execute(m, registry);
    }

    // 2. Market Buy that eats the entire ladder
    // Using 0.0 price for market-style or 1000.0 for aggressive limit
    auto t = createOrder(8888, 1, Side::BUY, 1000.0, (double)levels);
    book.execute(t, registry);

    // 3. Verify all levels are gone
    EXPECT_EQ(getLevelCount(Side::SELL), 0);
    EXPECT_DOUBLE_EQ(book.getLastTradedPrice(), 100.0 + levels - 1);
}