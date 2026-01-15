#include <gtest/gtest.h>
#include <vector>
#include "TradingEngine.hpp"
#include "Constants.hpp"

class OrderLifecycleSuite : public ::testing::Test {
protected:
    TradingEngine engine;

    // Standardized helper to extract the system-generated ID
    long getSystemOrderId(const EngineResponse& response) {
        if (!response.isSuccess()) {
            throw std::runtime_error("Engine returned failure: " + response.message);
        }
        return std::get<OrderAcknowledgement>(response.data).orderID;
    }

    // Helper to verify an order's state via Tag lookup
    void verifyActiveOrder(const std::string& tag, const std::string& sym, double expectedPrice, int expectedQty) {
        auto res = engine.getActiveOrderByTag(tag, sym);
        ASSERT_TRUE(res.isSuccess()) << "Failed to find active order with tag: " << tag;
        Order o = std::get<Order>(res.data);
        EXPECT_DOUBLE_EQ(o.price, expectedPrice);
        EXPECT_EQ(o.remainingQuantity, expectedQty);
    }
};

// ============================================================================
// 1. TAG-BASED PERSISTENCE AFTER PARTIAL FILL
// ============================================================================
TEST_F(OrderLifecycleSuite, TagLookupAfterPartialFill) {
    std::string sym = "BTC/USD";
    std::string makerTag = "MAKER_STAY";

    // 1. Submit resting order
    long mId = getSystemOrderId(engine.submitOrder(LimitOrderRequest{makerTag, sym, Side::SELL, 50, 100.0}));

    // 2. Take half of it
    engine.submitOrder(LimitOrderRequest{"TAKER", sym, Side::BUY, 20, 100.0});

    // 3. Verify via ID
    auto resId = engine.getActiveOrderById(mId);
    ASSERT_EQ(std::get<Order>(resId.data).remainingQuantity, 30);

    // 4. Verify via Tag (Crucial: Tag must still point to the same updated object)
    verifyActiveOrder(makerTag, sym, 100.0, 30);
}

// ============================================================================
// 2. TAG SCOPING ACROSS SYMBOLS
// ============================================================================
TEST_F(OrderLifecycleSuite, DuplicateTagsInDifferentSymbols) {
    /**
     * Requirement: The same Tag used in different instruments must remain 
     * isolated and searchable independently.
     */
    engine.submitOrder(LimitOrderRequest{"ALGO_1", "BTC/USD", Side::BUY, 10, 90.0});
    engine.submitOrder(LimitOrderRequest{"ALGO_1", "ETH/USD", Side::BUY, 10, 190.0});

    // Validate BTC lookup doesn't return ETH data
    verifyActiveOrder("ALGO_1", "BTC/USD", 90.0, 10);
    verifyActiveOrder("ALGO_1", "ETH/USD", 190.0, 10);
}

// ============================================================================
// 3. TAG CLEANUP AFTER FULL FILL
// ============================================================================
TEST_F(OrderLifecycleSuite, TagPurgeOnCompletion) {
    /**
     * Requirement: Once an order is fully filled, searching by Tag must fail.
     */
    std::string tag = "TEMP_ORDER";
    engine.submitOrder(LimitOrderRequest{tag, "BTC/USD", Side::SELL, 10, 100.0});
    
    // Fill it
    engine.submitOrder(LimitOrderRequest{"T", "BTC/USD", Side::BUY, 10, 100.0});

    // Lookup by Tag should now return a failure result
    auto res = engine.getActiveOrderByTag(tag, "BTC/USD");
    EXPECT_FALSE(res.isSuccess()) << "Tag " << tag << " should have been purged from registry.";
}

// ============================================================================
// 4. COMPREHENSIVE GULP: TAGS + EXECUTIONS + BOOK
// ============================================================================
TEST_F(OrderLifecycleSuite, GulpWithDetailedTagValidation) {
    std::string sym = "BTC/USD";

    // Level 1 Sellers
    long s1Id = getSystemOrderId(engine.submitOrder(LimitOrderRequest{"S1_TAG", sym, Side::SELL, 10, 100.0}));
    long s2Id = getSystemOrderId(engine.submitOrder(LimitOrderRequest{"S2_TAG", sym, Side::SELL, 10, 100.0}));

    // Aggressor sweeps both
    engine.submitOrder(MarketOrderRequest{"T_TAG", sym, Side::BUY, 20});

    // --- EXECUTION CHECK ---
    auto execs = std::get<std::vector<Execution>>(engine.getExecutions().data);
    ASSERT_EQ(execs.size(), 2);
    EXPECT_EQ(execs[0].sellTag, "S1_TAG");
    EXPECT_EQ(execs[1].sellTag, "S2_TAG");
    EXPECT_EQ(execs[0].buyTag, "T_TAG");

    // --- REGISTRY CHECK (BY TAG) ---
    EXPECT_FALSE(engine.getActiveOrderByTag("S1_TAG", sym).isSuccess());
    EXPECT_FALSE(engine.getActiveOrderByTag("S2_TAG", sym).isSuccess());

    // --- BOOK CHECK ---
    auto snap = std::get<OrderBookSnapshot>(engine.getOrderBook(sym, 1).data);
    EXPECT_TRUE(snap.asks.empty());
}