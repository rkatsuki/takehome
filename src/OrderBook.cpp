#include "OrderBook.hpp"

OrderBook::OrderBook(Symbol sym) : symbol(std::move(sym)) {
    // Reserve memory upfront to avoid mid-trade latency spikes
    bids.reserve(Config::MAX_PRICE_LEVELS / 2);
    asks.reserve(Config::MAX_PRICE_LEVELS / 2);
}

void OrderBook::placeOrder(std::shared_ptr<Order> order) {
    auto& targetSide = (order->side == Side::BUY) ? bids : asks;

    // 1. Binary search for the insertion point
    // Note: We use raw comparison here for the search logic (std::lower_bound needs it)
    auto it = std::lower_bound(targetSide.begin(), targetSide.end(), order->price,
        [&](const PriceLevel& lvl, double p) {
            if (order->side == Side::BUY) return lvl.price > p; // Bids: High to Low
            return lvl.price < p; // Asks: Low to High
        });

    // 2. Check for existence using Precision::equal (The Epsilon Check)
    // We must check if 'it' is valid AND if the price matches our epsilon
    bool levelExists = (it != targetSide.end() && Precision::equal(it->price, order->price));

    if (!levelExists) {
        // Create new level if epsilon check fails
        it = targetSide.insert(it, PriceLevel{order->price});
    }

    // 3. Update the Level Volume using Precision-safe addition logic if necessary
    // (Though simple addition is usually fine, we use totalVolume for snapshots)
    it->totalVolume += order->remainingQuantity;
    it->entries.push_back(OrderEntry{order->remainingQuantity, order});

    // 4. Update the Global Index
    idToLocation[order->orderID] = { 
        std::prev(it->entries.end()), 
        order->price, 
        order->side 
    };
}

// Updated: Uses OrderID (uint64_t)
std::optional<double> OrderBook::getRemainingQty(OrderID id) const {
    auto itLoc = idToLocation.find(id);
    if (itLoc == idToLocation.end()) return std::nullopt;

    const auto& [entryIt, price, side] = itLoc->second;
    const auto& targetSide = (side == Side::BUY) ? bids : asks;

    // Binary search for the price level
    auto itLevel = std::lower_bound(targetSide.begin(), targetSide.end(), price,
        [&](const PriceLevel& lvl, double p) {
            if (side == Side::BUY) return lvl.price > p;
            return lvl.price < p;
        });

    if (itLevel != targetSide.end() && Precision::equal(itLevel->price, price)) {
        // Double check: Does the entry still exist in this level?
        // Since we store a list iterator, we can access it directly.
        return entryIt->remainingQuantity;
    }

    return std::nullopt;
}

std::optional<double> OrderBook::cancelById(OrderID id) {
    // 1. O(1) Lookup to find where the order should be
    auto itLoc = idToLocation.find(id);
    if (itLoc == idToLocation.end()) return std::nullopt;

    // We retrieve the price and side (stable values) 
    // and the list iterator (stable for std::list)
    auto [entryIt, price, side] = itLoc->second;
    auto& targetSide = (side == Side::BUY) ? bids : asks;

    // 2. Binary search to find the PriceLevel in the vector
    auto itLevel = std::lower_bound(targetSide.begin(), targetSide.end(), price,
        [&](const PriceLevel& lvl, double p) {
            if (side == Side::BUY) return lvl.price > p;
            return lvl.price < p;
        });

    // 3. Verify the level matches our price using Precision
    if (itLevel != targetSide.end() && Precision::equal(itLevel->price, price)) {
        double removedQty = entryIt->remainingQuantity;
        
        Precision::subtract_or_zero(itLevel->totalVolume, removedQty);

        // Remove from the list (This is safe because it's std::list)
        itLevel->entries.erase(entryIt);
        
        // Remove from our global ID map
        idToLocation.erase(itLoc);

        // 4. Vector Compaction: If the price level is now empty, delete it
        if (itLevel->entries.empty()) {
            targetSide.erase(itLevel); 
            // Note: This shift is O(N), but for 20k levels, it's very fast.
        }
        
        return removedQty;
    }

    return std::nullopt;
}

MatchResult OrderBook::execute(std::shared_ptr<Order> taker, std::atomic<ExecID>& nextExecId) {
    MatchResult result{.takerOrderId = taker->orderID};

    if (taker->side == Side::BUY) matchAgainstBook(asks, taker, result, nextExecId);
    else matchAgainstBook(bids, taker, result, nextExecId);

    // 1. If there is meaningful quantity left after matching
    if (Precision::isPositive(taker->remainingQuantity)) {
        if (taker->type == OrderType::LIMIT) {
            placeOrder(taker); // Post to book
        } else {
            // Market Order ran out of liquidity
            std::unique_lock lock(taker->stateMutex);
            taker->status = OrderStatus::CANCELLED;
            // WE LEAVE remainingQuantity ALONE - as per your correct suggestion.
            // This allows the user to see exactly what didn't fill.
        }
    } 
    // 2. If the remainder is effectively zero (below Epsilon)
    else {
        std::unique_lock lock(taker->stateMutex);
        taker->status = OrderStatus::FILLED;
        taker->remainingQuantity = 0.0; // Snap "dust" to absolute zero
    }

    publishShadow();
    result.remainingQuantity = taker->remainingQuantity;
    return result; 
}

void OrderBook::publishShadow() {
    // Unique lock: Only one thread (the Matcher) writes to the shadow
    std::unique_lock lock(shadowMutex);
    
    shadow.sequence++;
    shadow.bids.clear();
    shadow.asks.clear();

    // Reserve to prevent reallocations during the sync
    shadow.bids.reserve(bids.size());
    shadow.asks.reserve(asks.size());

    // Linear walk through the live vector - highly cache-friendly!
    // Live 'bids' is already [500, 499, 498...] -> Index 0 is best
    for (const auto& level : bids) {
        shadow.bids.push_back({level.price, level.totalVolume});
    }
    // Live 'asks' is already [501, 502, 503...] -> Index 0 is best
    for (const auto& level : asks) {
        shadow.asks.push_back({level.price, level.totalVolume});
    }
}

OrderBookSnapshot OrderBook::getSnapshot(size_t depth) const {
    // Shared lock: Multiple API threads can snapshot while the Matcher is busy
    std::shared_lock lock(shadowMutex); 
    
    OrderBookSnapshot snap;
    snap.symbol = this->symbol;
    snap.updateSeq = shadow.sequence;

    // Helper to extract top 'depth' levels from shadow vectors
    auto copyTopLevels = [&](const std::vector<BookLevel>& src, std::vector<BookLevel>& dest) {
        size_t count = std::min(depth, src.size());
        dest.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            dest.push_back(src[i]);
        }
    };

    copyTopLevels(shadow.bids, snap.bids);
    copyTopLevels(shadow.asks, snap.asks);
    
    return snap;
}