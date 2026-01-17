#pragma once

#include <map>
#include <vector>
#include <memory>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <optional>
#include <unordered_map>
#include <mutex>

#include "Constants.hpp"
#include "Type.hpp" 

class OrderBook {
public:
    // Updated: Uses Symbol struct
    explicit OrderBook(Symbol sym);

    // Updated: nextExecId now uses ExecID (uint64_t)
    MatchResult execute(std::shared_ptr<Order> taker, std::atomic<ExecID>& nextExecId);

    [[nodiscard]] OrderBookSnapshot getSnapshot(size_t depth) const;
    
    // Updated: Takes OrderID (uint64_t)
    [[nodiscard]] std::optional<double> getRemainingQty(OrderID id) const;
    
    // Updated: Takes OrderID (uint64_t)
    std::optional<double> cancelById(OrderID id);

    double getLastPrice() const { 
        return lastMatchedPrice.load(std::memory_order_relaxed); 
    }

    size_t getPriceLevelCount() const {
        return bids.size() + asks.size();
    }

private:
    Symbol symbol; // Correctly uses the Symbol struct
    std::atomic<double> lastMatchedPrice{0.0};

    // LIVE VENUE
    // Bids: Sorted High -> Low | Asks: Sorted Low -> High
    std::vector<PriceLevel> bids; 
    std::vector<PriceLevel> asks;
    
    // Updated: Keyed by OrderID (uint64_t)
    std::unordered_map<OrderID, OrderLocation> idToLocation;

    // Helper to binary search the correct price level
    auto findLevel(std::vector<PriceLevel>& side, double price, Side orderSide);

    // SHADOW BUFFER
    mutable std::shared_mutex shadowMutex;
    ShadowBuffer shadow;

    void placeOrder(std::shared_ptr<Order> order);
    void publishShadow(); 

    // Internal Template - Updated to use ExecID
    template<typename SideVector>
    void matchAgainstBook(SideVector& targetSide, std::shared_ptr<Order> taker, 
                        MatchResult& result, std::atomic<ExecID>& nextExecId) {

        auto it = targetSide.begin();
        
        // Use the helper for consistency
        while (it != targetSide.end() && Precision::isPositive(taker->remainingQuantity)) {
            double levelPrice = it->price;

            if (taker->type == OrderType::LIMIT) {
                if (taker->side == Side::BUY) {
                    if (levelPrice > taker->price && !Precision::equal(levelPrice, taker->price)) break;
                } else {
                    if (levelPrice < taker->price && !Precision::equal(levelPrice, taker->price)) break;
                }
            }

            PriceLevel& level = *it;
            auto entryIt = level.entries.begin();

            while (entryIt != level.entries.end() && Precision::isPositive(taker->remainingQuantity)) {
                double matchQty = std::min(taker->remainingQuantity, entryIt->remainingQuantity);
                
                result.fills.push_back({
                    nextExecId.fetch_add(1, std::memory_order_relaxed),
                    levelPrice, matchQty, taker->orderID, entryIt->fatOrder->orderID
                });

                {
                    std::unique_lock lock(entryIt->fatOrder->stateMutex);
                    // Use subtract_or_zero to prevent drift
                    Precision::subtract_or_zero(entryIt->remainingQuantity, matchQty);
                    Precision::subtract_or_zero(entryIt->fatOrder->remainingQuantity, matchQty);
                    entryIt->fatOrder->cumulativeCost += (matchQty * levelPrice);
                    
                    if (Precision::isZero(entryIt->remainingQuantity)) {
                        entryIt->fatOrder->status = OrderStatus::FILLED;
                        entryIt->fatOrder->remainingQuantity = 0.0; // Hard zero
                    }
                }

                Precision::subtract_or_zero(taker->remainingQuantity, matchQty);
                taker->cumulativeCost += (matchQty * levelPrice);
                Precision::subtract_or_zero(level.totalVolume, matchQty);

                if (Precision::isZero(entryIt->remainingQuantity)) {
                    idToLocation.erase(entryIt->fatOrder->orderID);
                    entryIt = level.entries.erase(entryIt);
                } else {
                    ++entryIt;
                }
            }

            lastMatchedPrice.store(levelPrice, std::memory_order_relaxed);

            if (level.entries.empty()) {
                it = targetSide.erase(it);
            } else {
                break; 
            }
        }
    }
};