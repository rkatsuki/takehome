#include "TradingEngine.hpp"

// Constructor
TradingEngine::TradingEngine() 
    : nextOrderId(1), nextExecId(1) {
    // Initialization of sharded ID maps or monitors if necessary
    monitor.currentGlobalOrderCount.store(0);
}

// Helper to get current epoch in nanoseconds
int64_t TradingEngine::now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ============================================================================
// TRANSFORMERS: Creating internal Order entities from Requests
// ============================================================================

Order TradingEngine::transform(const LimitOrderRequest& req) {
    // Generate ID later in matchAndRecord to ensure sequence at the point of entry
    return { 
        0, req.tag, req.symbol, req.side, OrderType::LIMIT, 
        req.quantity, req.price, req.quantity, now() 
    };
}

Order TradingEngine::transform(const MarketOrderRequest& req) {
    return { 
        0, req.tag, req.symbol, req.side, OrderType::MARKET, 
        req.quantity, 0.0, req.quantity, now() 
    };
}

// ============================================================================
// CORE LOGIC: Order Submission & Matching
// ============================================================================

// Order Type   Matching Scenario   execs.size()    remQty  Registry?   submitOrder() Result
// Limit        Full Match          >0,             0.0     No,         Success (0)
// Limit        Partial Match       >0              >0.0    Yes         Success (0)
// Limit        No Match            0,              0.0     Yes         Success (0)
// Market       Partial/Full        >0              ≥0.0    No          Success (0)
// Market       No Match            0               >0.0    No          Failure (400)

EngineResponse TradingEngine::submitOrder(const LimitOrderRequest& req) {
    Order order = transform(req);
    return matchAndRecord(order);
}

EngineResponse TradingEngine::submitOrder(const MarketOrderRequest& req) {
    Order order = transform(req);
    return matchAndRecord(order);
}

EngineResponse TradingEngine::matchAndRecord(Order& order) {
    // 1. Symbol Validation
    if (!Config::isSupported(order.symbol)) {
        return {400, "Unsupported symbol", std::monostate{}};
    }

    // 2. Quantity Validation (Using Precision::EPSILON)
    // Prevents orders that are too small to be meaningful (Dust)
    if (order.quantity < Precision::EPSILON || order.quantity > Config::MAX_ORDER_QTY) {
        return {400, "Invalid or excessive quantity", std::monostate{}};
    }

    // 3. Tag Length Check
    if (order.tag.size() > Config::MAX_TAG_SIZE) {
        return {400, "Tag string exceeds maximum allowed length", std::monostate{}};
    }

    // 4. Global Capacity Check
    if (monitor.currentGlobalOrderCount.load(std::memory_order_relaxed) >= Config::MAX_GLOBAL_ORDERS) [[unlikely]] {
        return {503, "Engine at maximum capacity", std::monostate{}};
    }

    // 5. Price range validation (Limit Orders only)
    if (order.type == OrderType::LIMIT) {
        if (order.price < Config::MIN_ORDER_PRICE || order.price > Config::MAX_ORDER_PRICE) {
            return {400, "Invalid or excessive price", std::monostate{}};
        }
    }

    OrderBook* book = getBook(order.symbol);
    if (!book) return {400, "Invalid Symbol", std::monostate{}};

    // 6. Price banding validation
    double currentLastPrice = book->getLastPrice(); 
    if (order.type == OrderType::LIMIT && currentLastPrice > Precision::EPSILON) {
        double diff = std::abs(order.price - currentLastPrice);
        if (diff > (currentLastPrice * Config::PRICE_BAND_PERCENT)) {
            return {400, "Price outside banding limits", std::monostate{}};
        }
    }

    // 7. PRE-EXECUTION RECORDING
    // We assign the ID and map the symbol so the Matcher knows who owns the order
    order.orderID = nextOrderId.fetch_add(1);

    // 8. EXECUTION PHASE
    // Capture the size of execution history BEFORE the call to see if matches happen
    size_t beforeExecCount;
    {
        std::lock_guard<std::mutex> lock(execHistoryMutex);
        beforeExecCount = executionHistory.size();
    }

    book->execute(order, executionHistory, execHistoryMutex, nextExecId);

    // 9. POST-EXECUTION VALIDATION (The StateSuite Fix)
    bool matchedAny = false;
    {
        std::lock_guard<std::mutex> lock(execHistoryMutex);
        matchedAny = (executionHistory.size() > beforeExecCount);
    }

    // If it's a Market Order and nothing matched, it's a failure.
    if (order.type == OrderType::MARKET && !matchedAny) {
        // Clean up the shard mapping since the order is being discarded
        auto& shard = idShards[order.orderID % ID_SHARD_COUNT];
        {
            std::unique_lock lock(shard.mutex);
            shard.mapping.erase(order.orderID);
        }
        return {400, "Insufficient liquidity for market order", std::monostate{}};
    }

    // 10. FINAL SUCCESS RECORDING
    auto& shard = idShards[order.orderID % ID_SHARD_COUNT];
    {
        std::unique_lock lock(shard.mutex);
        shard.mapping[order.orderID] = order.symbol;
    }

    // Increment global monitor only if the order actually persists (is a LIMIT order)
    // or if we count every submission. Usually, we only count resting orders.
    if (order.type == OrderType::LIMIT) {
        monitor.currentGlobalOrderCount.fetch_add(1, std::memory_order_relaxed);
    }

    return {0, "Success", OrderAcknowledgement{order.orderID, order.tag}};
}

// ============================================================================
// RETRIEVAL & MAINTENANCE
// ============================================================================

OrderBook* TradingEngine::getBook(const std::string& symbol) {
    {
        std::shared_lock lock(bookshelfMutex);
        auto it = symbolBooks.find(symbol);
        if (it != symbolBooks.end()) return it->second.get();
    }

    // Lazy initialization of OrderBooks
    if (Config::isSupported(symbol)) {
        std::unique_lock lock(bookshelfMutex);
        if (symbolBooks.find(symbol) == symbolBooks.end()) {
            symbolBooks[symbol] = std::make_unique<OrderBook>(symbol);
        }
        return symbolBooks[symbol].get();
    }
    return nullptr;
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

EngineResponse TradingEngine::getExecutions() {
    std::lock_guard<std::mutex> lock(execHistoryMutex);
    auto batch = std::move(executionHistory);
    executionHistory.clear();
    return {0, "Success", std::move(batch)};
}

EngineResponse TradingEngine::getActiveOrderById(long orderId) {
    auto& shard = idShards[orderId % ID_SHARD_COUNT];
    std::string symbol;
    {
        std::shared_lock lock(shard.mutex);
        auto it = shard.mapping.find(orderId);
        if (it == shard.mapping.end()) return {1, "Order not found", std::monostate{}};
        symbol = it->second;
    }

    OrderBook* book = getBook(symbol);
    if (!book) return {500, "Internal Error", std::monostate{}};

    auto entryOpt = book->getActiveOrder(orderId);
    return entryOpt ? EngineResponse{0, "Success", *entryOpt} : EngineResponse{1, "Order no longer active"};
}

EngineResponse TradingEngine::getActiveOrderByTag(const std::string& tag, const std::string& symbol) {
    OrderBook* book = getBook(symbol);
    if (!book) return {404, "Symbol not found", std::monostate{}};

    auto entryOpt = book->getActiveOrderByTag(tag);
    return entryOpt ? EngineResponse{0, "Success", *entryOpt} : EngineResponse{1, "Tag not found"};
}
