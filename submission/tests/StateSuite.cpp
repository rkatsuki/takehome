#include <gtest/gtest.h>
#include "TradingEngine.hpp"

class StateSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

TEST_F(StateSuite, TagToSystemIdMapping) {
    // 1. Submit an order with ID 0 (asking the engine to generate one)
    LimitOrderRequest o = {"CLIENT_KEY_99", "BTC/USD", Side::BUY, 10, 50000.0};
    auto res = engine.submitOrder(o);
    
    ASSERT_TRUE(res.isSuccess());
    
    // 2. Extract the system-generated ID from the acknowledgement
    auto ack = std::get<OrderAcknowledgement>(res.data);
    long generatedId = ack.orderID;
    EXPECT_GT(generatedId, 0); // Ensure it's not the 0 we sent

    // 3. RETRIEVAL BY TAG: This is the core requirement
    // Prove that "CLIENT_KEY_99" now maps to the generated ID
    auto tagRes = engine.getActiveOrderByTag("CLIENT_KEY_99", "BTC/USD");
    ASSERT_TRUE(tagRes.isSuccess());
    
    Order found = std::get<Order>(tagRes.data);
    EXPECT_EQ(found.orderID, generatedId);
    EXPECT_EQ(found.tag, "CLIENT_KEY_99");
}

TEST_F(StateSuite, SystemIdAndTagCircularRetrieval) {
    // 1. Setup an order with ID 0 (Instruction)
    std::string myTag = "ALPHA_1";
    std::string mySymbol = "BTC/USD";
    LimitOrderRequest input = {myTag, mySymbol, Side::BUY, 10, 50000.0};

    // 2. Submit and capture the system-generated ID
    auto submitRes = engine.submitOrder(input);
    ASSERT_TRUE(submitRes.isSuccess()) << "Submission failed: " << submitRes.message;
    
    auto ack = std::get<OrderAcknowledgement>(submitRes.data);
    long systemId = ack.orderID;
    EXPECT_GT(systemId, 0) << "Engine failed to generate a valid OrderID";

    // 3. RETRIEVAL BY ID: Verify the Registry has it
    auto idRes = engine.getActiveOrderById(systemId);
    ASSERT_TRUE(idRes.isSuccess()) << "Could not find order by generated ID";
    
    Order orderById = std::get<Order>(idRes.data);
    EXPECT_EQ(orderById.orderID, systemId);
    EXPECT_EQ(orderById.tag, myTag);
    EXPECT_EQ(orderById.symbol, mySymbol); // Crucial for routing

    // 4. RETRIEVAL BY TAG: Verify the Tag-Map points to the same ID
    auto tagRes = engine.getActiveOrderByTag(myTag, mySymbol);
    ASSERT_TRUE(tagRes.isSuccess()) << "Could not find order by tag";
    
    Order orderByTag = std::get<Order>(tagRes.data);
    EXPECT_EQ(orderByTag.orderID, systemId); // Must match the system ID
    EXPECT_EQ(orderByTag.price, 50000.0);
}

TEST_F(StateSuite, TagOverwriteLogic) {
    // 1. Submit Order A with "TAG_X"
    engine.submitOrder(LimitOrderRequest{"TAG_X", "BTC/USD", Side::BUY, 10, 100.0});
    auto res1 = engine.getActiveOrderByTag("TAG_X", "BTC/USD");
    ASSERT_TRUE(res1.isSuccess());
    EXPECT_EQ(std::get<Order>(res1.data).tag, "TAG_X");
    
    // 2. Submit Order B with same "TAG_X"
    engine.submitOrder(LimitOrderRequest{"TAG_X", "BTC/USD", Side::BUY, 5, 50.0});
    auto res2 = engine.getActiveOrderByTag("TAG_X", "BTC/USD");
    ASSERT_TRUE(res2.isSuccess());
    EXPECT_EQ(std::get<Order>(res2.data).tag, "TAG_X");

    // orderid is unique
    EXPECT_NE(std::get<Order>(res1.data).orderID, std::get<Order>(res2.data).orderID);

    // 4. Order 1 should still be accessible by ID (It wasn't replaced, just the tag pointer moved)
    auto resID = engine.getActiveOrderById(std::get<Order>(res1.data).orderID);
    EXPECT_TRUE(resID.isSuccess());
    EXPECT_EQ(std::get<Order>(resID.data).tag, "TAG_X");
    EXPECT_EQ(std::get<Order>(resID.data).price, 100.0);
    EXPECT_EQ(std::get<Order>(resID.data).quantity, 10);
    EXPECT_EQ(std::get<Order>(resID.data).side, Side::BUY);
    EXPECT_EQ(std::get<Order>(resID.data).symbol, "BTC/USD");
}

TEST_F(StateSuite, PartialFillRegistryUpdate) {
    // 1. Place a large resting BUY order
    engine.submitOrder(LimitOrderRequest{"BUY_TAG", "BTC/USD", Side::BUY, 100, 100.0});

    // 2. Place a smaller aggressive SELL order to trigger a partial fill
    engine.submitOrder(LimitOrderRequest{"SELL_TAG", "BTC/USD", Side::SELL, 40, 100.0});

    // 3. Verify the resting order in the registry now shows 60 remaining
    auto res = engine.getActiveOrderByTag("BUY_TAG", "BTC/USD");
    Order o = std::get<Order>(res.data);
    EXPECT_EQ(o.remainingQuantity, 60);
    EXPECT_EQ(o.quantity, 100); // Original quantity should stay 100
}

TEST_F(StateSuite, CancellationCleansUpTagMap) {
    // 1. Submit order
    engine.submitOrder(LimitOrderRequest{"TEMP_TAG", "BTC/USD", Side::BUY, 10, 100.0});
    ASSERT_TRUE(engine.getActiveOrderByTag("TEMP_TAG", "BTC/USD").isSuccess());

    // 2. Cancel by ID
    engine.cancelOrderByTag("TEMP_TAG", "BTC/USD");

    // 3. Verify Tag lookup now fails (it shouldn't point to a ghost ID)
    auto res = engine.getActiveOrderByTag("TEMP_TAG", "BTC/USD");
    EXPECT_FALSE(res.isSuccess());
    EXPECT_EQ(res.message, "Tag not found");
}

TEST_F(StateSuite, MarketOrdersDoNotPersistInState) {
    // Submit a market order when there is no resting liquidity, it should fail.
    auto submitRes = engine.submitOrder(MarketOrderRequest{"MKT_TAG", "BTC/USD", Side::BUY, 10});
    EXPECT_FALSE(submitRes.isSuccess());

    // Try to get the order by tag, and it won't be there.
    auto getRes2 = engine.getActiveOrderByTag("MKT_TAG", "BTC/USD");
    EXPECT_FALSE(getRes2.isSuccess());
}

TEST_F(StateSuite, PriceLevelCleanupState) {
    // 1. Create a price level with one order
    engine.submitOrder(LimitOrderRequest{"TAG1", "BTC/USD", Side::BUY, 12, 150.0});

    // 2. Fully match it
    engine.submitOrder(LimitOrderRequest{"TAG2", "BTC/USD", Side::SELL, 12, 150.0});

    // 3. Get Snapshot and verify the 150.0 level is GONE (not just 0 volume, but erased)
    auto snapshotRes = engine.getOrderBook("BTC/USD", 5);
    auto snapshot = std::get<OrderBookSnapshot>(snapshotRes.data);

    for (const auto& level : snapshot.bids) {
        EXPECT_NE(level.price, 150.0) << "Price level 150.0 should have been erased from memory";
    }
    EXPECT_EQ(snapshot.lastPrice, 150.0);    
}

TEST_F(StateSuite, RegistryShardStressTest) {
    const int NUM_SHARDS = 8; // Match Config::NUM_SHARDS
    const int numOrders = 20; // Enough to ensure multiple orders per shard
    std::vector<long> generatedIds;

    // 1. Submit orders and collect the system-generated IDs
    for (int i = 0; i < numOrders; ++i) {
        LimitOrderRequest o = {"TAG_" + std::to_string(i), "BTC/USD", Side::BUY, 1, 100.0};
        auto res = engine.submitOrder(o);
        
        ASSERT_TRUE(res.isSuccess());
        auto ack = std::get<OrderAcknowledgement>(res.data);
        generatedIds.push_back(ack.orderID);
    }

    // 2. Verify every single order is retrievable
    // This proves the sharding logic (ID % NUM_SHARDS) is consistent 
    // for both writing (submit) and reading (getActiveOrderById).
    for (long id : generatedIds) {
        auto res = engine.getActiveOrderById(id);
        EXPECT_TRUE(res.isSuccess()) << "Failed to find system-generated ID: " << id;
        
        Order found = std::get<Order>(res.data);
        EXPECT_EQ(found.orderID, id);
    }
}

TEST_F(StateSuite, InvalidLookupsReturnError) {
    // 1. Lookup an ID that hasn't been generated yet
    auto resId = engine.getActiveOrderById(999999);
    EXPECT_FALSE(resId.isSuccess());

    // 2. Lookup a tag that doesn't exist
    auto resTag = engine.getActiveOrderByTag("NON_EXISTENT_TAG", "BTC/USD");
    EXPECT_FALSE(resTag.isSuccess());
}