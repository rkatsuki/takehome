#include "TradingEngine.hpp"

TradingEngine::TradingEngine() {}

OrderBook* TradingEngine::getBook(const std::string& symbol) {
    {
        std::shared_lock lock(bookshelfMutex);
        auto it = symbolBooks.find(symbol);
        if (it != symbolBooks.end()) return it->second.get();
    }

    if (Config::TRADED_SYMBOLS.find(symbol) != Config::TRADED_SYMBOLS.end()) {
        std::unique_lock lock(bookshelfMutex);
        if (symbolBooks.find(symbol) == symbolBooks.end()) {
            symbolBooks[symbol] = std::make_unique<OrderBook>(symbol);
        }
        return symbolBooks[symbol].get();
    }
    return nullptr;
}

EngineResponse TradingEngine::submitOrder(const Order& order) {
    // 1. Symbol Validation
    if (Config::TRADED_SYMBOLS.find(order.symbol) == Config::TRADED_SYMBOLS.end()) {
        return {400, "Unsupported symbol", std::monostate{}};
    }

    // 2. Quantity Validation
    if (order.quantity <= 0 || order.quantity > Config::MAX_ORDER_QTY) {
        return {400, "Invalid or excessive quantity", std::monostate{}};
    }

    // 3. Tag Length Check
    if (order.tag.size() > Config::MAX_TAG_SIZE) {
        return {400, "Tag string exceeds maximum allowed length", std::monostate{}};
    }

    // 4. Global Capacity Check (Total Engine Load)
    // You could track this with an atomic counter in TradingEngine
    if (monitor.currentGlobalOrderCount.load(std::memory_order_relaxed) >= Config::MAX_GLOBAL_ORDERS) [[unlikely]] {
        return {503, "Engine at maximum capacity", std::monostate{}};
    }

    // 5. Price range validation
    if (order.price < Config::MIN_ORDER_PRICE || order.price > Config::MAX_ORDER_PRICE) {
        return {400, "Invalid or excessive price", std::monostate{}};
    }

    OrderBook* book = getBook(order.symbol);
    if (!book) return {404, "Invalid Symbol", std::monostate{}};

    // 6. Price banding validation; this limits the clutter in OrderBook
    double currentLastPrice = book->getLastPrice(); 
    // if currentLastPrice==0 then the first trade is yet to happen, and band is not applied.
    if (currentLastPrice > 0.0) {
        // Perform banding check
        double diff = std::abs(order.price - currentLastPrice);
        if (diff > (currentLastPrice * Config::PRICE_BAND_PERCENT)) {
            return {400, "Price outside banding limits", std::monostate{}};
        }
    }

    Order mutableOrder = order;
    mutableOrder.orderID = nextOrderId.fetch_add(1);

    auto& shard = idShards[mutableOrder.orderID % ID_SHARD_COUNT];
    {
        std::unique_lock lock(shard.mutex);
        shard.mapping[mutableOrder.orderID] = mutableOrder.symbol;
    }

    book->execute(mutableOrder, executionHistory, execHistoryMutex, nextExecId);
    return {0, "Success", OrderAcknowledgement{mutableOrder.orderID, mutableOrder.tag}};
}

EngineResponse TradingEngine::cancelOrderByTag(const std::string& tag, const std::string& symbol) {
    OrderBook* book = getBook(symbol);
    if (!book) return {404, "Symbol Not Found", std::monostate{}};
    return book->cancelByTag(tag) ? EngineResponse{0, "Success"} : EngineResponse{1, "Tag Not Found"};
}

EngineResponse TradingEngine::cancelOrderById(long orderId) {
    auto& shard = idShards[orderId % ID_SHARD_COUNT];
    std::string symbol;
    {
        std::shared_lock lock(shard.mutex);
        auto it = shard.mapping.find(orderId);
        if (it == shard.mapping.end()) return {1, "ID Not Found"};
        symbol = it->second;
    }

    OrderBook* book = getBook(symbol);
    if (book && book->cancelById(orderId)) {
        std::unique_lock lock(shard.mutex);
        shard.mapping.erase(orderId);
        return {0, "Success"};
    }
    return {1, "Cancel Failed"};
}

EngineResponse TradingEngine::getOrderBook(const std::string& symbol, int depth) {
    OrderBook* book = getBook(symbol);
    return book ? EngineResponse{0, "Success", book->getSnapshot(depth)} : EngineResponse{404, "Invalid Symbol"};
}

EngineResponse TradingEngine::reportExecutions() {
    std::lock_guard<std::mutex> lock(execHistoryMutex);
    auto batch = std::move(executionHistory);
    executionHistory.clear();
    return {0, "Success", std::move(batch)};
}

EngineResponse TradingEngine::getActiveOrderById(long orderId) {
    // 1. Route to the correct symbol via ID Shards
    auto& shard = idShards[orderId % ID_SHARD_COUNT];
    std::string symbol;
    {
        std::shared_lock lock(shard.mutex);
        auto it = shard.mapping.find(orderId);
        if (it == shard.mapping.end()) {
            return {1, "Order not found", std::monostate{}};
        }
        symbol = it->second;
    }

    // 2. Get raw data from the book
    OrderBook* book = getBook(symbol);
    if (!book) return {500, "Internal Error", std::monostate{}};

    auto entryOpt = book->getActiveOrder(orderId);
    
    // 3. Wrap in EngineResponse
    if (entryOpt) {
        return {0, "Success", *entryOpt};
    }
    return {1, "Order no longer active", std::monostate{}};
}

EngineResponse TradingEngine::getActiveOrderByTag(const std::string& tag, const std::string& symbol) {
    OrderBook* book = getBook(symbol);
    if (!book) return {404, "Symbol not found", std::monostate{}};

    auto entryOpt = book->getActiveOrderByTag(tag);

    if (entryOpt) {
        return {0, "Success", *entryOpt};
    }
    return {1, "Tag not found", std::monostate{}};
}