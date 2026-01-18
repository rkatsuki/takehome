#include "TradingEngine.hpp"

#include <iostream>

/**
 * @brief Logic Orchestrator
 * @details The Engine operates on the "Pinned Thread" principle. By ensuring 
 * only one thread ever calls processCommand(), we remove all internal locking 
 * requirements (Lock-Free State), maximizing L1/L2 cache efficiency.
 */
TradingEngine::TradingEngine(OutputHandler& handler) 
    : outputHandler_(handler)
{
    /**
     * @note Memory Pre-allocation:
     * Reserving the registry size at startup prevents "Rehash Storms."
     * In a live market, a map resize can cause a latency spike of several 
     * milliseconds, which is unacceptable for HFT.
     */
    registry_.reserve(Config::MAX_GLOBAL_ORDERS / 100);
}

void TradingEngine::processCommand(Command& cmd) {
    /**
     * @note Single-Writer Pattern:
     * We have zero mutexes here. This thread "owns" the OrderBooks and Registry.
     * External threads communicate only via the ThreadSafeQueues.
     */
    switch (cmd.type) {
        case CommandType::NEW: [[likely]] {
            
            // 1. HARD LIMIT GUARDRAIL
            // Prevents malicious or buggy clients from exhausting system memory.
            if (registry_.size() >= static_cast<size_t>(Config::MAX_GLOBAL_ORDERS)) [[unlikely]] {
                outputHandler_.logError("REJECT: Global order limit reached");
                return;
            }

            // 2. STATUTORY VALIDATION
            // Check for negative prices, zero quantity, etc., before touching the book.
            std::string errorReason;
            if (!validateOrder(cmd, errorReason)) [[unlikely]] {
                outputHandler_.logError(std::format("REJECT: User {} Order {} - {}", 
                                       cmd.userId, cmd.userOrderId, errorReason));
                return;
            }
            
            // 3. BOOK ROUTING
            // Find the specific symbol book. Complexity: O(log N) for map search.
            OrderBook& book = getOrCreateBook(cmd.symbol);
            
            /** * @note ACKNOWLEDGMENT (Requirement):
             * We acknowledge immediately. If the match takes time (e.g., a massive 
             * sweep), the client knows their order was at least accepted.
             */
            outputHandler_.printAck(cmd.userId, cmd.userOrderId);

            // 4. CORE EXECUTION
            // Delegate the Price-Time priority logic to the symbol-specific book.
            book.execute(cmd, registry_);
            break;
        }

        case CommandType::CANCEL: [[likely]] {
            OrderKey key{cmd.userId, cmd.userOrderId};
            
            /**
             * @note Registry-First Lookup:
             * We don't know which book the order is in. The Registry tells us 
             * the Symbol/Price/Side in O(1) time.
             */
            auto it = registry_.find(key);
            
            if (it != registry_.end()) [[likely]] {
                OrderBook& book = getOrCreateBook(it->second.symbol);
                book.cancel(key, registry_);
            } else [[unlikely]] {
                outputHandler_.logError(std::format("REJECT_CANCEL: Order {}:{} not found", 
                                        cmd.userId, cmd.userOrderId));
            }
            break;
        }

        case CommandType::FLUSH: [[unlikely]] {
            handleFlush();
            break;
        }
    }
}

/**
 * @brief Global State Reset
 * @details Instead of deleting OrderBook objects (which triggers expensive 
 * deallocations), we clear their internal structures to reuse the memory.
 */
void TradingEngine::handleFlush() {
    for (auto& [symbol, bookPtr] : books_) {
        bookPtr->clear(); 
    }
    
    // Clear the registry mapping to allow UserOrderIDs to be reused.
    registry_.clear();
    
    outputHandler_.logError("ENGINE_EVENT: Global state reset (Books pooled).");
}

/**
 * @brief Dynamic Book Discovery
 * @details If a new symbol appears in the UDP stream, we instantiate a new 
 * book for it. In a production system, these are often pre-created for 
 * known tickers to avoid runtime allocation.
 */
OrderBook& TradingEngine::getOrCreateBook(Symbol symbol) {
    auto it = books_.find(symbol);
    
    if (it == books_.end()) [[unlikely]] {
        /**
         * @note std::unique_ptr:
         * We use smart pointers to ensure that even if the engine crashes, 
         * all memory associated with the books is freed by the RAII pattern.
         */
        auto [newIt, success] = books_.emplace(symbol, 
                                std::make_unique<OrderBook>(symbol, outputHandler_));
        return *(newIt->second);
    }
    
    return *(it->second);
}



/**
 * @brief Validates an incoming command against system and market guardrails.
 * @details This is the 'Firewall' of the engine. It ensures that no invalid state
 * can reach the OrderBook, maintaining deterministic behavior.
 */
bool TradingEngine::validateOrder(const Command& cmd, std::string& outError) {
    
    // 1. Basic Guardrails (Always apply)
    if (!Config::isSupported(cmd.symbol.data)) [[unlikely]] {
        outError = "INVALID_SYMBOL";
        return false;
    }

    if (registry_.size() >= static_cast<size_t>(Config::MAX_GLOBAL_ORDERS)) [[unlikely]] {
        outError = "SYSTEM_CAPACITY_EXCEEDED";
        return false;
    }

    // Quantity is required for both Market and Limit orders
    if (cmd.quantity < Config::MIN_ORDER_QTY - Precision::EPSILON) [[unlikely]] {
        outError = "QUANTITY_TOO_LOW";
        return false;
    }

    /**
        * @note ARCHITECTURAL DECISION: Market vs Limit Branching
        * If price is 0.0, it is a Market Order. We skip corridor and 
        * price-level checks because Market orders do not sit on the book.
        */
    if (!Precision::isZero(cmd.price)) {
        
        // Limit Price Magnitude Check
        if (cmd.price < Config::MIN_ORDER_PRICE - Precision::EPSILON || 
            cmd.price > Config::MAX_ORDER_PRICE + Precision::EPSILON) [[unlikely]] {
            outError = "PRICE_OUT_OF_BOUNDS";
            return false;
        }

        /**
         * @logic FIX: Skip corridor if LTP is 0.0 (No trades yet)
         * This allows the first trade of the day to set the "anchor" price.
         */
        OrderBook& book = getOrCreateBook(cmd.symbol); 
        double ltp = book.getLastTradedPrice();
        if (!Precision::isZero(ltp)) {
            double lowerBound = ltp * (1.0 - Config::PRICE_CORRIDOR_THRESHOLD);
            double upperBound = ltp * (1.0 + Config::PRICE_CORRIDOR_THRESHOLD);

            if (cmd.price < lowerBound - Precision::EPSILON || 
                cmd.price > upperBound + Precision::EPSILON) [[unlikely]] {
                outError = "PRICE_OUTSIDE_VOLATILITY_CORRIDOR";
                return false;
            }
        }

        // Price Level Capacity Check
        // Market orders don't create levels, so we only check this for Limit orders.
        if (book.isFull() && !book.hasLevel(cmd.price)) [[unlikely]] {
            outError = "MAX_ORDERBOOK_PRICE_LEVELS_REACHED";
            return false;
        }
    }
    return true; 
}