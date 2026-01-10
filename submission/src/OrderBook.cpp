#include "OrderBook.hpp"

void OrderBook::setCallback(CompletionCallback cb) {
    onComplete_ = std::move(cb); 
}

void OrderBook::processOrder(const Order& order) {
    std::unique_lock lock(bookMutex_);

    // 1. Handle Cancellation
    if (order.type == OrderType::CANCEL) {
        auto it = orders_.find(order.orderID);
        if (it != orders_.end()) {
            if (onComplete_) onComplete_(order.orderID, it->second.tag, it->second);
            
            // Handle different map types separately to avoid ternary operator errors
            if (it->second.side == Side::BUY) {
                auto& vec = bids_[it->second.price];
                vec.erase(std::remove(vec.begin(), vec.end(), order.orderID), vec.end());
                if (vec.empty()) bids_.erase(it->second.price);
            } else {
                auto& vec = asks_[it->second.price];
                vec.erase(std::remove(vec.begin(), vec.end(), order.orderID), vec.end());
                if (vec.empty()) asks_.erase(it->second.price);
            }
            orders_.erase(it);
        }
        return;
    }

    // 2. Matching Logic Helper (Lambda)
    // We use a lambda to handle the two different map types (bids_ vs asks_)
    long remQty = order.quantity;
    
    auto matchAgainst = [&](auto& oppSideMap) {
        while (remQty > 0 && !oppSideMap.empty()) {
            auto it = oppSideMap.begin(); // Best price
            
            // Price protection for Limit Orders
            if (order.type == OrderType::LIMIT) {
                if (order.side == Side::BUY && it->first > order.price) break;
                if (order.side == Side::SELL && it->first < order.price) break;
            }

            auto& queue = it->second;
            auto qIt = queue.begin();
            
            while (remQty > 0 && qIt != queue.end()) {
                long restingId = *qIt;
                auto& restingOrder = orders_[restingId];
                
                long tradeQty = std::min(remQty, restingOrder.remainingQuantity);
                
                // Record Execution (ExecID is tracked per book)
                executions_.push_back({nextExecId_++, order.orderID, restingId, it->first, tradeQty});
                
                remQty -= tradeQty;
                restingOrder.remainingQuantity -= tradeQty;

                if (restingOrder.remainingQuantity == 0) {
                    if (onComplete_) onComplete_(restingId, restingOrder.tag, restingOrder);
                    orders_.erase(restingId);
                    qIt = queue.erase(qIt); 
                } else {
                    // Resting order still has qty, so aggressor must be empty
                    break; 
                }
            }
            if (queue.empty()) oppSideMap.erase(it);
        }
    };

    // Execute matching based on side
    if (order.side == Side::BUY) {
        matchAgainst(asks_);
    } else {
        matchAgainst(bids_);
    }

    // 3. Post-Match: Add remainder to book or notify completion
    if (remQty > 0 && order.type == OrderType::LIMIT) {
        orders_[order.orderID] = {order.price, remQty, order.quantity, order.side, order.tag};
        if (order.side == Side::BUY) {
            bids_[order.price].push_back(order.orderID);
        } else {
            asks_[order.price].push_back(order.orderID);
        }
    } else if (remQty == 0 || order.type == OrderType::MARKET) {
        if (onComplete_) {
            OrderEntry finalState{order.price, remQty, order.quantity, order.side, order.tag};
            onComplete_(order.orderID, order.tag, finalState);
        }
    }
}

std::optional<OrderEntry> OrderBook::getOrderDetails(long id) const {
    std::shared_lock lock(bookMutex_);
    auto it = orders_.find(id);
    if (it != orders_.end()) return it->second;
    return std::nullopt;
}

std::vector<Execution> OrderBook::flushExecutions() {
    std::unique_lock lock(bookMutex_);
    return std::move(executions_); // std::move clears the source automatically
}

BookTop OrderBook::getTop() const {
    std::shared_lock lock(bookMutex_);
    BookTop top;
    if (!bids_.empty()) {
        top.bestBid = bids_.begin()->first;
        top.bidLevels = bids_.size();
    }
    if (!asks_.empty()) {
        top.bestAsk = asks_.begin()->first;
        top.askLevels = asks_.size();
    }
    return top;
}