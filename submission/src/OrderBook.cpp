#include "OrderBook.hpp"

OrderBook::OrderBook(std::string sym) : symbol(std::move(sym)) {}

void OrderBook::execute(Order& incoming, std::vector<Execution>& globalExecs, std::mutex& execMutex, std::atomic<long>& nextExecId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Execution> localExecs;

    // 1. MATCHING PHASE
    // Branching here allows the template to instantiate for the specific map type
    if (incoming.side == Side::BUY) {
        matchAgainstSide(incoming, asks, localExecs, nextExecId);
    } else {
        matchAgainstSide(incoming, bids, localExecs, nextExecId);
    }

    // 2. RESTING PHASE
    if (incoming.quantity > 0 && incoming.type == OrderType::LIMIT) {
        if (incoming.side == Side::BUY) {
            auto& bucket = bids[incoming.price];
            bucket.totalVolume += incoming.quantity;
            bucket.entries.push_back({incoming.price, incoming.quantity, incoming.quantity, incoming.side, incoming.tag, incoming.orderID});
            
            OrderRegistry::Location loc{incoming.side, incoming.price, std::prev(bucket.entries.end())};
            registry.record(incoming.orderID, incoming.tag, loc);
        } else {
            auto& bucket = asks[incoming.price];
            bucket.totalVolume += incoming.quantity;
            bucket.entries.push_back({incoming.price, incoming.quantity, incoming.quantity, incoming.side, incoming.tag, incoming.orderID});
            
            OrderRegistry::Location loc{incoming.side, incoming.price, std::prev(bucket.entries.end())};
            registry.record(incoming.orderID, incoming.tag, loc);
        }
    }

    // 3. GLOBAL SYNC
    if (!localExecs.empty()) {
        std::lock_guard<std::mutex> gLock(execMutex);
        globalExecs.insert(globalExecs.end(), 
                           std::make_move_iterator(localExecs.begin()), 
                           std::make_move_iterator(localExecs.end()));
    }
}

bool OrderBook::cancelByTag(const std::string& tag) {
    // Registry lookup for tag is thread-safe within OrderRegistry
    auto locOpt = registry.getLocationByTag(tag);
    if (!locOpt) return false;

    // Delegate to the surgical ID-based cancellation
    return cancelById(locOpt->it->orderID);
}

bool OrderBook::cancelById(long orderId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto locOpt = registry.getLocation(orderId);
    if (!locOpt) return false;

    auto& loc = *locOpt;
    
    // The Generic Lambda
    auto cleaner = [&](auto& sideMap) -> bool {
        auto it = sideMap.find(loc.price);
        if (it == sideMap.end()) return false;
        
        it->second.totalVolume -= loc.it->remainingQuantity;
        std::string tagToCleanup = loc.it->tag;
        sideMap[loc.price].entries.erase(loc.it); 
        
        if (it->second.entries.empty()) sideMap.erase(it);
        registry.remove(orderId, tagToCleanup);
        return true;
    };

    return (loc.side == Side::BUY) ? cleaner(bids) : cleaner(asks);
}

OrderBookSnapshot OrderBook::getSnapshot(int depth) const {
    std::lock_guard<std::mutex> lock(bookMutex);
    OrderBookSnapshot snap;
    snap.symbol = symbol;
    
    auto collect = [&](auto const& mapSource, std::vector<PriceLevel>& vecDest) {
        int count = 0;
        for (auto const& [price, bucket] : mapSource) {
            if (count++ >= depth) break;
            vecDest.push_back({price, bucket.totalVolume});
        }
    };

    collect(bids, snap.bids);
    collect(asks, snap.asks);
    return snap;
}

std::optional<OrderEntry> OrderBook::getActiveOrder(long orderId) {
    std::lock_guard<std::mutex> lock(bookMutex);
    
    auto locOpt = registry.getLocation(orderId);
    if (!locOpt) return std::nullopt;

    return *(locOpt->it);
}

std::optional<OrderEntry> OrderBook::getActiveOrderByTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(bookMutex);
    
    auto locOpt = registry.getLocationByTag(tag);
    if (!locOpt) return std::nullopt;

    return *(locOpt->it);
}

template <typename T>
void OrderBook::matchAgainstSide(Order& incoming, T& targets, std::vector<Execution>& localExecs, std::atomic<long>& nextExecId) {
    while (incoming.quantity > 0 && !targets.empty()) {
        auto it = targets.begin(); 
        const double restingPrice = it->first;

        // Inclusive Price Check (Fixed: Buy >= Sell match)
        if (incoming.type == OrderType::LIMIT) {
            if (incoming.side == Side::BUY && restingPrice > incoming.price) break; 
            if (incoming.side == Side::SELL && restingPrice < incoming.price) break;
        }

        auto& bucket = it->second;
        for (auto entryIt = bucket.entries.begin(); entryIt != bucket.entries.end(); ) {
            if (incoming.quantity <= 0) break;

            auto& resting = *entryIt;
            long matchQty = std::min(incoming.quantity, resting.remainingQuantity);

            this->updateLastPrice(restingPrice);
            
            Execution e;
            e.executionID = nextExecId.fetch_add(1);
            e.aggressorOrderID = incoming.orderID;
            e.restingOrderID = resting.orderID;
            e.symbol = this->symbol;
            e.price = restingPrice; 
            e.quantity = matchQty;

            if (incoming.side == Side::BUY) {
                e.buyTag = incoming.tag; e.sellTag = resting.tag;
            } else {
                e.buyTag = resting.tag; e.sellTag = incoming.tag;
            }
            localExecs.push_back(std::move(e));

            incoming.quantity -= matchQty;
            resting.remainingQuantity -= matchQty;
            bucket.totalVolume -= matchQty;

            if (resting.remainingQuantity == 0) {
                registry.remove(resting.orderID, resting.tag);
                entryIt = bucket.entries.erase(entryIt);
            } else {
                ++entryIt;
            }
        }

        if (bucket.entries.empty()) {
            targets.erase(it);
        } else {
            break; 
        }
    }
}

template void OrderBook::matchAgainstSide(Order&, std::map<double, OrderBook::PriceBucket, std::less<double>>&, std::vector<Execution>&, std::atomic<long>&);
template void OrderBook::matchAgainstSide(Order&, std::map<double, OrderBook::PriceBucket, std::greater<double>>&, std::vector<Execution>&, std::atomic<long>&);

double OrderBook::getLastPrice() const {
    return lastPrice.load(std::memory_order_relaxed);
}

void OrderBook::updateLastPrice(double price) {
    lastPrice.store(price, std::memory_order_relaxed);
}