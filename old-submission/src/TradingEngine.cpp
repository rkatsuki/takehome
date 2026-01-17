#include "TradingEngine.hpp"

TradingEngine::TradingEngine() : nextExecId(1000000) {}

// ============================================================================
// SECTION 1: ORDER INGRESS (SUBMISSION)
// ============================================================================

EngineResponse TradingEngine::submitOrder(const LimitOrderRequest& req) {
    auto val = validateCommon(req.symbol, req.quantity, req.price, req.tag); 
    if (!val.isSuccess()) return val;

    // Use Symbol struct directly
    auto order = std::make_shared<Order>(
        req.price, req.quantity, req.quantity, 0.0, 
        req.side, OrderType::LIMIT, OrderStatus::ACTIVE, 
        req.symbol, req.tag
    );
    return processOrder(order);
}

EngineResponse TradingEngine::submitOrder(const MarketOrderRequest& req) {
    auto val = validateCommon(req.symbol, req.quantity, std::nullopt, req.tag);
    if (!val.isSuccess()) return val;

    auto order = std::make_shared<Order>(
        0.0, req.quantity, req.quantity, 0.0, 
        req.side, OrderType::MARKET, OrderStatus::ACTIVE, 
        req.symbol, req.tag
    );
    return processOrder(order);
}

EngineResponse TradingEngine::validateCommon(const Symbol& symbol, double quantity, 
                                             std::optional<double> price, const std::string& tag) {
    if (quantity <= 0 || quantity > Config::MAX_ORDER_QTY) {
        return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Invalid quantity");
    }

    if (tag.size() > Config::MAX_TAG_SIZE) {
        return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Tag too long");
    }

    if (symbol.empty()) {
        return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Invalid symbol");
    }

    {
        std::shared_lock lock(registryMutex);
        if (idRegistry.size() >= Config::MAX_GLOBAL_ORDERS) {
            return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Engine at max capacity");
        }
    }

    if (price.has_value()) {
        double p = *price;
        if (p < Config::MIN_ORDER_PRICE || p > Config::MAX_ORDER_PRICE) {
            return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Price out of range");
        }

        if (OrderBook* book = tryGetBook(symbol)) {
            if (book->getPriceLevelCount() >= Config::MAX_PRICE_LEVELS) {
                return EngineResponse::Error(EngineStatusCode::VALIDATION_FAILURE, "Orderbook too fragmented");
            }

            double lastPrice = book->getLastPrice();
            if (lastPrice > 0.0) {
                double band = lastPrice * Config::PRICE_BAND_PERCENT;
                if (p > (lastPrice + band) || p < (lastPrice - band)) {
                    return EngineResponse::Error(EngineStatusCode::PRICE_OUT_OF_BAND, "Price outside banding limits");
                }
            }
        }
    }

    return EngineResponse::Success("Validated");
}

EngineResponse TradingEngine::processOrder(std::shared_ptr<Order> order) {
    {
        std::unique_lock lock(registryMutex);
        if (tagToId.contains(order->tag)) {
            return EngineResponse::Error(EngineStatusCode::DUPLICATE_TAG, "Tag collision");
        }
        tagToId[order->tag] = order->orderID;
        idRegistry[order->orderID] = order;
    }

    OrderBook* book = getOrAddBook(order->symbol);
    MatchResult result = book->execute(order, nextExecId);
    
    return finalizeExecution(result, order);
}

EngineResponse TradingEngine::finalizeExecution(const MatchResult& result, std::shared_ptr<Order> taker) {
    std::string msg;
    if (taker->status == OrderStatus::FILLED) {
        msg = "Order fully filled";
    } else if (result.fills.empty()) {
        msg = (taker->type == OrderType::MARKET) ? "Market order cancelled (No Liquidity)" : "Order posted to book";
    } else {
        msg = "Order partially filled";
    }

    return EngineResponse::Success(std::move(msg), taker);
}

// ============================================================================
// SECTION 2: MANAGEMENT & INFRASTRUCTURE
// ============================================================================

EngineResponse TradingEngine::internalCancel(OrderID orderId) {
    auto it = idRegistry.find(orderId);
    if (it == idRegistry.end()) return EngineResponse::Error(EngineStatusCode::ORDER_ID_NOT_FOUND, "ID missing");

    std::shared_ptr<Order> order = it->second;
    if (order->isFinished()) return EngineResponse::Error(EngineStatusCode::ALREADY_TERMINAL, "Already terminal");

    if (OrderBook* book = tryGetBook(order->symbol)) {
        auto cancelledQty = book->cancelById(order->orderID);
        
        if (cancelledQty.has_value()) {
            std::unique_lock lock(order->stateMutex); 
            order->status = OrderStatus::CANCELLED;
            order->remainingQuantity = *cancelledQty;
            return EngineResponse::Success("Cancelled");
        }
    }
    return EngineResponse::Error(EngineStatusCode::ORDER_ID_NOT_FOUND, "Not active in book");
}

OrderBook* TradingEngine::getOrAddBook(const Symbol& symbol) {
    {
        std::shared_lock lock(bookshelfMutex);
        auto it = symbolBooks.find(symbol);
        if (it != symbolBooks.end()) return it->second.get();
    }
    std::unique_lock lock(bookshelfMutex);
    auto& book = symbolBooks[symbol];
    if (!book) book = std::make_unique<OrderBook>(symbol);
    return book.get();
}

OrderBook* TradingEngine::tryGetBook(const Symbol& symbol) const {
    std::shared_lock lock(bookshelfMutex);
    auto it = symbolBooks.find(symbol);
    return (it != symbolBooks.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// SECTION 3: PUBLIC API WRAPPERS
// ============================================================================

EngineResponse TradingEngine::getOrder(OrderID id) {
    std::shared_lock lock(registryMutex);
    auto it = idRegistry.find(id);
    if (it == idRegistry.end()) return EngineResponse::Error(EngineStatusCode::ORDER_ID_NOT_FOUND, "ID missing");

    std::shared_ptr<Order> order = it->second;
    
    // The Handshake: Check the live book if the order is still active
    if (!order->isFinished()) {
        if (OrderBook* book = tryGetBook(order->symbol)) {
            auto liveQty = book->getRemainingQty(order->orderID);
            if (liveQty.has_value()) {
                order->remainingQuantity = *liveQty;
            }
        }
    }
    
    return EngineResponse::Success("Success", order);
}

EngineResponse TradingEngine::cancelOrder(OrderID id) {
    // Note: Mutex management is handled inside internalCancel and registry logic
    return internalCancel(id);
}

EngineResponse TradingEngine::getOrderByTag(const std::string& tag) {
    OrderID id = 0;
    {
        std::shared_lock lock(registryMutex);
        auto it = tagToId.find(tag);
        if (it == tagToId.end()) return EngineResponse::Error(EngineStatusCode::TAG_NOT_FOUND, "Tag not found");
        id = it->second;
    }
    return getOrder(id);
}

EngineResponse TradingEngine::cancelOrderByTag(const std::string& tag) {
    OrderID id = 0;
    {
        std::shared_lock lock(registryMutex);
        auto it = tagToId.find(tag);
        if (it == tagToId.end()) return EngineResponse::Error(EngineStatusCode::TAG_NOT_FOUND, "Tag not found");
        id = it->second;
    }
    return internalCancel(id);
}

EngineResponse TradingEngine::getOrderBookSnapshot(const Symbol& symbol, size_t depth) {
    OrderBook* book = tryGetBook(symbol);
    if (!book) return EngineResponse::Error(EngineStatusCode::SYMBOL_NOT_FOUND, "Symbol missing");

    OrderBookSnapshot snap = book->getSnapshot(depth);
    EngineResponse resp = EngineResponse::Success("Success");
    resp.snapshot = std::move(snap);
    return resp;
}