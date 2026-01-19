#pragma once

#include <map>
#include <list>
#include <unordered_map>
#include <vector>

#include "Types.hpp"
#include "OutputHandler.hpp"

/**
 * @brief High-Performance L3 Order Book
 * @details Manages Bid/Ask state using price-sorted maps.
 * Uses cache-friendly alignment for core validation members.
 */
class OrderBook {
    friend class OrderBookTestSuite;
private:
    /**
     * @note ARCHITECTURAL DECISION: Data Locality
     * Placing lastTradedPrice_ and symbol_ at the top ensures they are 
     * likely on the same cache line as the object pointer, speeding up 
     * validateOrder() calls in the TradingEngine.
     */
    double lastTradedPrice_ = 0.0; 
    Symbol symbol_;
    OutputHandler& outputHandler_;

    // Bid Map: Sorted High-to-Low (std::greater)
    std::map<double, PriceLevel, std::greater<double>> bids_;
    
    // Ask Map: Sorted Low-to-High (std::less)
    std::map<double, PriceLevel, std::less<double>> asks_;

    // Last published BBO states for delta tracking
    BBO lastBid_{-1.0, 0.0};
    BBO lastAsk_{-1.0, 0.0};

    /**
     * @brief Publishes BBO updates only if the top-of-book has changed.
     */
    void checkAndPublishBBO(bool force) noexcept;

    /**
     * @brief Logic for matching a Taker against Maker liquidity.
     */
    template<typename T>
    void matchAgainstSide(Command& taker, T& makerSide, 
                          std::unordered_map<OrderKey, OrderLocation>& registry) noexcept;

    /**
     * @brief Logic for resting a residual order on the book.
     */
    template<typename T>
    void restOrderOnBook(T& sideMap, Command& cmd, Side side, 
                         std::unordered_map<OrderKey, OrderLocation>& registry) noexcept;

    /**
     * @brief Logic for removing a specific order node.
     */
    template<typename T>
    void removeFromSide(T& sideMap, const OrderLocation& loc, const OrderKey& key, 
                        std::unordered_map<OrderKey, OrderLocation>& registry,
                        std::unordered_map<OrderKey, OrderLocation>::iterator regIt) noexcept;

public:
    /**
     * @brief Constructor
     * @param symbol Fixed-width ticker symbol
     * @param handler Reference to the shared output tape
     */
    OrderBook(Symbol symbol, OutputHandler& handler);

    // Disable copying for performance and ownership clarity
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    /**
     * @brief Accessor for the dynamic volatility corridor center.
     * @return The price of the last execution or 0.0 if no trades have occurred.
     */
    double getLastTradedPrice() const noexcept { return lastTradedPrice_; }
    
    /**
     * @brief Updates the corridor center after a successful match.
     */
    void setLastTradedPrice(double price) noexcept { lastTradedPrice_ = price; }

    /**
     * @brief Main entry point for NEW orders.
     */
    void execute(Command& cmd, std::unordered_map<OrderKey, OrderLocation>& registry) noexcept;

    /**
     * @brief O(1) Cancellation path.
     */
    void cancel(const OrderKey& key, std::unordered_map<OrderKey, OrderLocation>& registry) noexcept;

    /**
     * @brief Wipes the book state without deallocating the object.
     */
    void clear() noexcept;

    /**
     * @brief Structural check against Config limits.
     */
    bool isFull() const noexcept;

    /**
     * @brief O(log N) price level existence check.
     */
    bool hasLevel(double price) const noexcept;
};