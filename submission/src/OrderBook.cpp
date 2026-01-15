#include "OrderBook.hpp"

OrderBook::OrderBook(std::string sym) : symbol(std::move(sym)) {}

// --- RESTORED: CANCELLATION LOGIC ---

bool OrderBook::cancelByTag(const std::string& tag) {
    // Registry lookup for tag is thread-safe within OrderRegistry
    auto locOpt = registry.getLocationByTag(tag);
    if (!locOpt) return false;

    // Delegate to the surgical ID-based cancellation using the ID found
    return cancelById(locOpt->it->orderID);
}

bool OrderBook::cancelById(long orderId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto locOpt = registry.getLocation(orderId);
    if (!locOpt) return false;

    auto& loc = *locOpt;
    
    // Internal helper to clean the specific map (bids or asks)
    auto cleaner = [&](auto& sideMap) -> bool {
        auto it = sideMap.find(loc.price);
        if (it == sideMap.end()) return false;
        
        // Use our Precision helper to avoid "ghost" volume
        Precision::subtract_or_zero(it->second.totalVolume, loc.it->remainingQuantity);
        
        std::string tagToCleanup = loc.it->tag;
        
        // Erase from the list (iterator remains valid for this call)
        it->second.entries.erase(loc.it); 
        
        // If the price level is now empty, prune the map
        if (it->second.entries.empty()) {
            sideMap.erase(it);
        }
        
        // Remove from the multi-index registry
        registry.remove(orderId, tagToCleanup);
        return true;
    };

    return (loc.side == Side::BUY) ? cleaner(bids) : cleaner(asks);
}

// --- RESTORED: GETTERS & SNAPSHOTS ---

OrderBookSnapshot OrderBook::getSnapshot(int depth) const {
    std::lock_guard<std::mutex> lock(bookMutex);
    OrderBookSnapshot snap;
    snap.symbol = symbol;
    snap.lastPrice = this->getLastPrice(); 
    snap.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     std::chrono::high_resolution_clock::now().time_since_epoch()).count();
                         
    auto collect = [&](auto const& mapSource, std::vector<PriceLevel>& vecDest) {
        int count = 0;
        for (auto const& [price, bucket] : mapSource) {
            if (count++ >= depth) break;
            // Push the aggregate double volume
            vecDest.push_back({price, bucket.totalVolume});
        }
    };

    collect(bids, snap.bids);
    collect(asks, snap.asks);
    return snap;
}

std::optional<Order> OrderBook::getActiveOrder(long orderId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto locOpt = registry.getLocation(orderId);
    if (!locOpt) return std::nullopt;
    return *(locOpt->it);
}

std::optional<Order> OrderBook::getActiveOrderByTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto locOpt = registry.getLocationByTag(tag);
    if (!locOpt) return std::nullopt;
    return *(locOpt->it);
}

double OrderBook::getLastPrice() const {
    return lastPrice.load(std::memory_order_relaxed);
}

void OrderBook::updateLastPrice(double price) {
    lastPrice.store(price, std::memory_order_relaxed);
}

// --- EXISTING: EXECUTE & MATCHING CORE ---

void OrderBook::execute(Order& incoming, std::vector<Execution>& globalExecs, std::mutex& execMutex, std::atomic<long>& nextExecId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Execution> localExecs;

    if (incoming.side == Side::BUY) {
        matchAgainstSide(incoming, asks, localExecs, nextExecId);
    } else {
        matchAgainstSide(incoming, bids, localExecs, nextExecId);
    }

    if (incoming.remainingQuantity > Precision::EPSILON && incoming.type == OrderType::LIMIT) {
        if (incoming.side == Side::BUY) {
            auto& bucket = bids[incoming.price];
            bucket.totalVolume += incoming.remainingQuantity;
            bucket.entries.push_back(incoming); 
            OrderRegistry::Location loc{incoming.side, incoming.price, std::prev(bucket.entries.end())};
            registry.record(incoming.orderID, incoming.tag, loc);
        } else {
            auto& bucket = asks[incoming.price];
            bucket.totalVolume += incoming.remainingQuantity;
            bucket.entries.push_back(incoming);
            OrderRegistry::Location loc{incoming.side, incoming.price, std::prev(bucket.entries.end())};
            registry.record(incoming.orderID, incoming.tag, loc);
        }
    }

    if (!localExecs.empty()) {
        std::lock_guard<std::mutex> gLock(execMutex);
        globalExecs.insert(globalExecs.end(), 
                           std::make_move_iterator(localExecs.begin()), 
                           std::make_move_iterator(localExecs.end()));
    }
}

template <typename MapType>
void OrderBook::matchAgainstSide(Order& incoming, MapType& targets, std::vector<Execution>& localExecs, std::atomic<long>& nextExecId) {
    auto it = targets.begin();
    while (it != targets.end() && incoming.remainingQuantity > Precision::EPSILON) {
        double bestPrice = it->first;
        bool canMatch = (incoming.type == OrderType::MARKET);
        if (!canMatch) {
            if (incoming.side == Side::BUY) {
                canMatch = (incoming.price > bestPrice) || Precision::equal(incoming.price, bestPrice);
            } else {
                canMatch = (incoming.price < bestPrice) || Precision::equal(incoming.price, bestPrice);
            }
        }

        if (!canMatch) break; 

        auto& bucket = it->second;
        auto entryIt = bucket.entries.begin();

        while (entryIt != bucket.entries.end() && incoming.remainingQuantity > Precision::EPSILON) {
            double matchQty = std::min(incoming.remainingQuantity, entryIt->remainingQuantity);
            long execId = nextExecId.fetch_add(1);
            
            localExecs.push_back(generateExecution(incoming, *entryIt, matchQty, execId, bestPrice));
            updateLastPrice(bestPrice);
            
            Precision::subtract_or_zero(incoming.remainingQuantity, matchQty);
            Precision::subtract_or_zero(entryIt->remainingQuantity, matchQty);
            Precision::subtract_or_zero(bucket.totalVolume, matchQty);

            if (entryIt->remainingQuantity == 0.0) {
                registry.remove(entryIt->orderID, entryIt->tag);
                entryIt = bucket.entries.erase(entryIt);
            } else {
                ++entryIt;
            }
        }

        if (bucket.entries.empty()) {
            it = targets.erase(it);
        } else {
            ++it;
        }
    }
}

Execution OrderBook::generateExecution(const Order& aggressor, const Order& maker, 
                                        double qty, long execId, double matchPrice) {
    Execution e;
    e.executionID = execId;
    e.aggressorOrderID = aggressor.orderID;
    e.restingOrderID = maker.orderID;
    e.aggressorSide = aggressor.side;
    e.symbol = maker.symbol;
    e.price = matchPrice;
    e.quantity = qty;
    
    if (aggressor.side == Side::BUY) {
        e.buyTag = aggressor.tag;
        e.sellTag = maker.tag;
    } else {
        e.buyTag = maker.tag;
        e.sellTag = aggressor.tag;
    }
    
    e.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    
    return e;
}

// Explicit template instantiations for the two map types
template void OrderBook::matchAgainstSide(Order&, std::map<double, OrderBook::PriceBucket, std::greater<double>>&, std::vector<Execution>&, std::atomic<long>&);
template void OrderBook::matchAgainstSide(Order&, std::map<double, OrderBook::PriceBucket, std::less<double>>&, std::vector<Execution>&, std::atomic<long>&);