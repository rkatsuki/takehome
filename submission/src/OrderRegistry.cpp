#include "OrderRegistry.hpp"

void OrderRegistry::registerOrder(long id, const std::string& tag, const std::string& symbol) {
    std::unique_lock lock(activeMutex_);
    idToSymbol_[id] = symbol;
    idToTag_[id] = tag;
    tagToId_[tag] = id;
    tagToSymbol_[tag] = symbol;
}

std::optional<OrderLocation> OrderRegistry::unregisterById(long id) {
    std::unique_lock lock(activeMutex_);
    auto it = idToTag_.find(id);
    if (it != idToTag_.end()) {
        std::string tag = it->second;
        std::string symbol = idToSymbol_[id];
        
        OrderLocation loc{id, symbol, tag};
        
        // Cleanup all maps
        idToTag_.erase(it);
        idToSymbol_.erase(id);
        tagToId_.erase(tag);
        tagToSymbol_.erase(tag);
        
        return loc;
    }
    return std::nullopt;
}

std::optional<OrderLocation> OrderRegistry::unregisterByTag(const std::string& tag) {
    std::unique_lock lock(activeMutex_);
    auto it = tagToId_.find(tag);
    if (it != tagToId_.end()) {
        long id = it->second;
        std::string symbol = tagToSymbol_[tag];
        
        OrderLocation loc{id, symbol, tag};
        
        // Cleanup all maps
        tagToId_.erase(it);
        tagToSymbol_.erase(tag);
        idToTag_.erase(id);
        idToSymbol_.erase(id);
        
        return loc;
    }
    return std::nullopt;
}

std::optional<OrderLocation> OrderRegistry::getLocationByTag(const std::string& tag) const {
    std::shared_lock lock(activeMutex_);
    auto it = tagToId_.find(tag);
    if (it != tagToId_.end()) {
        return OrderLocation{it->second, tagToSymbol_.at(tag), tag};
    }
    return std::nullopt;
}

std::optional<OrderLocation> OrderRegistry::getLocationById(long id) const {
    std::shared_lock lock(activeMutex_);
    auto it = idToTag_.find(id);
    if (it != idToTag_.end()) {
        return OrderLocation{id, idToSymbol_.at(id), it->second};
    }
    return std::nullopt;
}

void OrderRegistry::recordHistory(const std::string& tag, const std::string& symbol, double price, long qty, const std::string& status) {
    std::unique_lock lock(historyMutex_);
    
    // Eviction policy if history is full
    if (history_.size() >= MAX_HISTORY) {
        std::string oldestTag = historyOrderQueue_.front();
        historyOrderQueue_.pop_front();
        history_.erase(oldestTag);
    }
    
    history_[tag] = {symbol, price, qty, status};
    historyOrderQueue_.push_back(tag);
}

void OrderRegistry::printHistoryIfExists(const std::string& tag) const {
    std::shared_lock lock(historyMutex_);
    auto it = history_.find(tag);
    if (it != history_.end()) {
        std::cout << "STATUS_HISTORY," << tag << "," << it->second.symbol << "," 
                  << it->second.price << "," << it->second.originalQty << "," 
                  << it->second.status << std::endl;
    } else {
        std::cout << "STATUS_NOT_FOUND," << tag << std::endl;
    }
}