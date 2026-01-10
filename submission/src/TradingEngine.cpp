#include "TradingEngine.hpp"

OrderBook& TradingEngine::getOrCreateBook(const std::string& symbol) {
    // 1. Shared lock to check if book exists (Fast Path)
    {
        std::shared_lock lock(engineMutex_);
        auto it = symbolBooks_.find(symbol);
        if (it != symbolBooks_.end()) return *it->second;
    }

    // 2. Unique lock to create the book if missing (Slow Path)
    std::unique_lock lock(engineMutex_);
    auto it = symbolBooks_.find(symbol); // Double-check after acquiring unique_lock
    if (it == symbolBooks_.end()) {
        // Updated: Removed <Order> template
        auto book = std::make_unique<OrderBook>();
        
        // Define the completion callback
        // This is triggered when an order is fully FILLED or CANCELLED
        book->setCallback([this, symbol](long id, const std::string& tag, const OrderEntry& state) {
            // Clean up registry
            this->registry_.unregisterById(id);
            
            // Move to history
            std::string status = (state.remainingQuantity == 0) ? "FILLED" : "CANCELLED";
            this->registry_.recordHistory(tag, symbol, state.price, state.originalQuantity, status);
        });

        auto& bookRef = *book;
        symbolBooks_[symbol] = std::move(book);
        return bookRef;
    }
    return *it->second;
}

void TradingEngine::executeOrder(Order& order) {
    // Register the intent in the registry first
    // Note: orderID is already expected to be set or generated here
    registry_.registerOrder(order.orderID, order.tag, order.symbol);

    // Route to the correct book
    getOrCreateBook(order.symbol).processOrder(order);
}

void TradingEngine::executeCancelById(long orderId) {
    // 1. Ask registry where this order is
    auto loc = registry_.getLocationById(orderId);
    
    if (loc) {
        Order cancelReq;
        cancelReq.orderID = orderId;
        cancelReq.type = OrderType::CANCEL;
        cancelReq.symbol = loc->symbol;
        cancelReq.tag = loc->tag;

        getOrCreateBook(loc->symbol).processOrder(cancelReq);
    } else {
        std::cerr << "CANCEL_REJECTED,ID_NOT_FOUND," << orderId << std::endl;
    }
}

void TradingEngine::executeCancelByTag(const std::string& tag) {
    // 1. Ask registry where this tag is
    auto loc = registry_.getLocationByTag(tag);
    
    if (loc) {
        Order cancelReq;
        cancelReq.orderID = loc->orderID;
        cancelReq.type = OrderType::CANCEL;
        cancelReq.symbol = loc->symbol;
        cancelReq.tag = tag;

        getOrCreateBook(loc->symbol).processOrder(cancelReq);
    } else {
        std::cerr << "CANCEL_REJECTED,TAG_NOT_FOUND," << tag << std::endl;
    }
}

void TradingEngine::printOrderStateByTag(const std::string& tag) const {
    // Step 1: Check Active Registry for routing info
    auto loc = registry_.getLocationByTag(tag);
    
    if (loc) {
        std::shared_lock lock(engineMutex_);
        auto it = symbolBooks_.find(loc->symbol);
        if (it != symbolBooks_.end()) {
            auto details = it->second->getOrderDetails(loc->orderID);
            if (details) {
                std::cout << "STATUS_ACTIVE," << tag << "," << loc->symbol << "," 
                          << (details->side == Side::BUY ? "BUY" : "SELL") << ","
                          << details->price << "," << details->remainingQuantity 
                          << "/" << details->originalQuantity << std::endl;
                return;
            }
        }
    }

    // Step 2: Fallback to History if not found in any active book
    registry_.printHistoryIfExists(tag);
}

void TradingEngine::reportExecutions() {
    std::shared_lock lock(engineMutex_);
    for (auto& [symbol, book] : symbolBooks_) {
        auto trades = book->flushExecutions();
        for (const auto& e : trades) {
            // Standard CSV-like output for trades
            std::cout << "TRADE," << symbol << "," << e.aggressorOrderID 
                      << "," << e.restingOrderID << "," << e.price 
                      << "," << e.quantity << std::endl;
        }
    }
}

void TradingEngine::printOrderBookState(const std::string& symbol) const {
    std::shared_lock lock(engineMutex_);
    auto it = symbolBooks_.find(symbol);
    
    if (it != symbolBooks_.end()) {
        BookTop top = it->second->getTop(); 
        
        std::cout << "ORDERBOOK_SNAPSHOT," << symbol 
                  << ",BestBid:" << top.bestBid << "[" << top.bidLevels << "]"
                  << ",BestAsk:" << top.bestAsk << "[" << top.askLevels << "]" 
                  << std::endl;
    } else {
        std::cerr << "BOOK_ERROR,NOT_FOUND," << symbol << std::endl;
    }
}