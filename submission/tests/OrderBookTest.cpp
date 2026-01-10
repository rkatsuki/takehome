#include <gtest/gtest.h>
#include "OrderBook.hpp"

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook book;

    void SetUp() override {
        // Reset or initialize before each test if needed
    }
};

// Test 1: Ensure Limit Orders at the same price are matched FIFO (Time Priority)
TEST_F(OrderBookTest, TimePriorityTest) {
    // 1. Post two Sell orders at the same price
    Order sell1{1, "TAG1", "AAPL", Side::SELL, OrderType::LIMIT, 150.0, 100};
    Order sell2{2, "TAG2", "AAPL", Side::SELL, OrderType::LIMIT, 150.0, 100};
    
    book.processOrder(sell1);
    book.processOrder(sell2);

    // 2. Post a Buy order that only fills the first one
    Order buy1{3, "TAG3", "AAPL", Side::BUY, OrderType::LIMIT, 150.0, 100};
    book.processOrder(buy1);

    auto trades = book.flushExecutions();
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].restingOrderID, 1); // Should match with ID 1, not 2
    EXPECT_EQ(trades[0].quantity, 100);
}

// Test 2: Ensure Price Priority (Better prices match first)
TEST_F(OrderBookTest, PricePriorityTest) {
    // 1. Post two Sell orders at different prices
    Order sell1{1, "TAG1", "AAPL", Side::SELL, OrderType::LIMIT, 155.0, 100};
    Order sell2{2, "TAG2", "AAPL", Side::SELL, OrderType::LIMIT, 150.0, 100}; // Better price
    
    book.processOrder(sell1);
    book.processOrder(sell2);

    // 2. Buy order comes in
    Order buy1{3, "TAG3", "AAPL", Side::BUY, OrderType::LIMIT, 160.0, 100};
    book.processOrder(buy1);

    auto trades = book.flushExecutions();
    
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].restingOrderID, 2); // Should match with the cheaper 150.0 order
}

// Test 3: Market Order Matching
TEST_F(OrderBookTest, MarketOrderMatch) {
    Order sell{1, "TAG1", "AAPL", Side::SELL, OrderType::LIMIT, 100.0, 50};
    book.processOrder(sell);

    Order buyMarket{2, "TAG2", "AAPL", Side::BUY, OrderType::MARKET, 0.0, 50};
    book.processOrder(buyMarket);

    auto trades = book.flushExecutions();
    ASSERT_EQ(trades.size(), 1);
    EXPECT_EQ(trades[0].price, 100.0);
}