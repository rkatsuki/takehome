#include <gtest/gtest.h>
#include "TradingEngine.hpp"

class StateSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(StateSuite, TagOverwriteLogic) {
    // 1. Submit Order A with "TAG_X"
    engine.submitOrder({0, "TAG_X", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    auto res1 = engine.getActiveOrderByTag("TAG_X", "BTC/USD");
    ASSERT_TRUE(res1.isSuccess());
    EXPECT_EQ(std::get<OrderEntry>(res1.data).tag, "TAG_X");
    
    // 2. Submit Order B with same "TAG_X"
    engine.submitOrder({0, "TAG_X", "BTC/USD", Side::BUY, OrderType::LIMIT, 50.0, 5});
    auto res2 = engine.getActiveOrderByTag("TAG_X", "BTC/USD");
    ASSERT_TRUE(res2.isSuccess());
    EXPECT_EQ(std::get<OrderEntry>(res2.data).tag, "TAG_X");

    // orderid is unique
    EXPECT_NE(std::get<OrderEntry>(res1.data).orderID, std::get<OrderEntry>(res2.data).orderID);

    // 4. Order 1 should still be accessible by ID (It wasn't replaced, just the tag pointer moved)
    auto resID = engine.getActiveOrderById(std::get<OrderEntry>(res1.data).orderID);
    EXPECT_TRUE(resID.isSuccess());
    EXPECT_EQ(std::get<OrderEntry>(resID.data).tag, "TAG_X");
    EXPECT_EQ(std::get<OrderEntry>(resID.data).price, 100.0);
    EXPECT_EQ(std::get<OrderEntry>(resID.data).originalQuantity, 10);
    EXPECT_EQ(std::get<OrderEntry>(resID.data).side, Side::BUY);
}

TEST_F(StateSuite, ShardAndCounterIntegrity) {
    // 1. Test Duplicate ID rejection
    engine.submitOrder({1, "T1", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    auto res = engine.submitOrder({1, "T1", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    EXPECT_NE(res.statusCode, 0); // Should fail

    // 2. Test Surgical Cancellation (Mid-List)
    engine.submitOrder({10, "T10", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    engine.submitOrder({11, "T11", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    engine.submitOrder({12, "T12", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});

    // Cancel the middle one
    engine.cancelOrderById(11);
    EXPECT_FALSE(engine.getActiveOrderById(11).isSuccess());
    EXPECT_TRUE(engine.getActiveOrderById(10).isSuccess());
    EXPECT_TRUE(engine.getActiveOrderById(12).isSuccess());

    // 3. Modulo Sharding Check
    // IDs 1 and 17 should fall into the same shard (if ShardCount=16)
    // We check that acting on one doesn't affect the other
    engine.submitOrder({1, "T1", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    engine.submitOrder({17, "T17", "BTC/USD", Side::BUY, OrderType::LIMIT, 100.0, 10});
    engine.cancelOrderById(1);
    EXPECT_TRUE(engine.getActiveOrderById(17).isSuccess());
}