#include "OrderBook.hpp"

#include <algorithm>

/**
 * @brief Constructor
 * @param symbol The ticker this book manages (e.g., "AAPL")
 * @param handler Shared output tape for trades and acknowledgments
 */
OrderBook::OrderBook(Symbol symbol, OutputHandler& handler) 
    : symbol_(symbol), outputHandler_(handler) {
    // Note: bids_ and asks_ are initialized with their respective 
    // std::greater and std::less comparators via the header definition.
}

/**
 * @brief The Core Hot-Path Execution
 * @details This is the entry point for every NEW order. It follows a 
 * strict Taker-then-Maker sequence to ensure immediate liquidity 
 * consumption before resting the residual.
 */
void OrderBook::execute(Command& cmd, std::unordered_map<OrderKey, OrderLocation>& registry) noexcept {
    
    // Requirement: Every order must be acknowledged upon receipt.
    // We do this before matching to minimize Taker-perceived latency.
    if (cmd.type == CommandType::NEW) [[likely]] {
        outputHandler_.printAck(cmd.userId, cmd.userOrderId);

        /**
         * @phase 1: TAKER PHASE
         * Match incoming order against the opposite side of the book.
         * BUY matches against ASKS; SELL matches against BIDS.
         */
        if (cmd.side == Side::BUY) [[likely]] {
            matchAgainstSide(cmd, asks_, registry);
        } else {
            matchAgainstSide(cmd, bids_, registry);
        }

        /**
         * @phase 2: MAKER PHASE
         * Residual Handling. If the order still has quantity after the 
         * matching sweep, it becomes a "Maker" (Resting Order).
         */
        if (Precision::isPositive(cmd.quantity) && Precision::isPositive(cmd.price)) [[likely]] {
            if (cmd.side == Side::BUY) {
                restOrderOnBook(bids_, cmd, Side::BUY, registry);
            } else {
                restOrderOnBook(asks_, cmd, Side::SELL, registry);
            }
        }
    }

    // Every execution potentially changes the top of book.
    checkAndPublishBBO();
}

/**
 * @brief O(1) Cancellation via Registry
 * @details We bypass searching the map. The registry provides a direct 
 * iterator to the specific node in the PriceLevel's std::list.
 */
void OrderBook::cancel(const OrderKey& key, std::unordered_map<OrderKey, OrderLocation>& registry) noexcept {
    auto regIt = registry.find(key);
    if (regIt == registry.end()) [[unlikely]] return;

    const OrderLocation& loc = regIt->second;
    
    // Explicit branching to handle different Map types (std::less vs std::greater)
    if (loc.side == Side::BUY) {
        removeFromSide(bids_, loc, key, registry, regIt);
    } else {
        removeFromSide(asks_, loc, key, registry, regIt);
    }
}

/**
 * @brief Best Bid/Offer Delta Tracking
 * @details Compares current state vs last published state. This prevents 
 * redundant I/O messages when fills happen deep in the book without 
 * touching the Best Price/Volume.
 */
void OrderBook::checkAndPublishBBO() noexcept {
    auto updateBBO = [&](auto& sideMap, BBO& lastBBO, char sideCode) {
        // Price maps are sorted; begin() is always the 'Best' price.
        BBO current = sideMap.empty() ? BBO{-1.0, 0.0} : 
                                        BBO{sideMap.begin()->first, sideMap.begin()->second.totalVolume};
        
        if (current.price != lastBBO.price || current.volume != lastBBO.volume) [[likely]] {
            outputHandler_.printBBO(sideCode, current.price, current.volume);
            lastBBO = current;
        }
    };

    updateBBO(bids_, lastBid_, 'B');
    updateBBO(asks_, lastAsk_, 'S');
}

/**
 * @brief State Reset
 * @details Clears structures without deallocating the Book object itself.
 */
void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    lastBid_ = {-1.0, 0.0};
    lastAsk_ = {-1.0, 0.0};
}

/**
 * @brief Structural check for the OrderBook.
 * @return True if the book has hit the tree-depth limit.
 */
bool OrderBook::isFull() const noexcept {
    // We check both sides. If the total unique price points exceed 
    // the limit, we stop accepting new price levels to protect latency.
    return (bids_.size() + asks_.size()) >= static_cast<size_t>(Config::MAX_PRICE_LEVELS);
}

/**
 * @brief Check if a price level already exists.
 * @details If the level exists, we can always add more volume to it 
 * without increasing the structural complexity (the map size).
 */
bool OrderBook::hasLevel(double price) const noexcept {
    return bids_.contains(price) || asks_.contains(price);
}

/**
 * @brief The Core Matching Algorithm
 * @tparam T The map type (std::map with std::greater or std::less)
 * @details Leverages the map's internal comparator to execute price-crossing 
 * logic without explicit 'Side' branching, optimizing the hot path.
 */
template<typename T>
void OrderBook::matchAgainstSide(Command& taker, T& makerSide, std::unordered_map<OrderKey, OrderLocation>& registry) noexcept {
    // Extract the comparator (e.g., std::less or std::greater)
    typename T::key_compare isBetter = makerSide.key_comp();

    while (!makerSide.empty() && Precision::isPositive(taker.quantity)) {
        auto levelIt = makerSide.begin(); 
        const double makerPrice = levelIt->first;

        /**
         * PRICE CROSS CHECK
         * We match if the prices "cross" (Taker is aggressive enough).
         * We BREAK if the Maker's price is outside the Taker's limit.
         */
        if (Precision::isPositive(taker.price)) {
            if (taker.side == Side::BUY) {
                // Taker Buy: Stop if Maker Sell is HIGHER than our limit
                // Epsilon-safe: Only break if makerPrice is strictly > taker.price
                if (Precision::isGreater(makerPrice, taker.price)) {
                    break;
                }
            } else {
                // Taker Sell: Stop if Maker Buy is LOWER than our limit
                // Epsilon-safe: Only break if makerPrice is strictly < taker.price
                if (Precision::isLess(makerPrice, taker.price)) {
                    break;
                }
            }
        }

        PriceLevel& level = levelIt->second;
        auto orderIt = level.orders.begin(); 

        while (orderIt != level.orders.end() && Precision::isPositive(taker.quantity)) {
            const double tradeQty = std::min(taker.quantity, orderIt->remainingQuantity);

            /**
             * TRADE EXECUTION
             * We still use taker.side for the printTrade output to ensure the 
             * Taker/Maker IDs are in the correct CSV columns.
             */
            if (taker.side == Side::BUY) [[likely]] {
                outputHandler_.printTrade(taker.userId, taker.userOrderId, orderIt->userId, orderIt->userOrderId, makerPrice, tradeQty);
            } else {
                outputHandler_.printTrade(orderIt->userId, orderIt->userOrderId, taker.userId, taker.userOrderId, makerPrice, tradeQty);
            }
            setLastTradedPrice(makerPrice);

            // Update quantities using Epsilon-safe math from Precision namespace
            Precision::subtract_or_zero(taker.quantity, tradeQty);
            Precision::subtract_or_zero(orderIt->remainingQuantity, tradeQty);
            Precision::subtract_or_zero(level.totalVolume, tradeQty);

            // Fully filled Maker orders are removed from Book and Registry.
            if (Precision::isZero(orderIt->remainingQuantity)) {
                registry.erase({orderIt->userId, orderIt->userOrderId});
                orderIt = level.orders.erase(orderIt); 
            } else {
                ++orderIt; // Partial fill: Maker stays, Taker exhausted.
            }
        }

        // Cleanup empty price levels to maintain O(log N) search efficiency.
        if (level.orders.empty()) [[unlikely]] {
            makerSide.erase(levelIt);
        }
    }
}

/**
 * @brief Internal Helper: Maker Placement
 * @details Places an order into the bid/ask map and registers its location.
 */
template<typename T>
void OrderBook::restOrderOnBook(T& sideMap, Command& cmd, Side side, 
                                std::unordered_map<OrderKey, OrderLocation>& registry) noexcept {
    auto [levelIt, inserted] = sideMap.try_emplace(cmd.price);
    auto& level = levelIt->second;
    
    if (inserted) [[unlikely]] {
        level.price = cmd.price;
        level.totalVolume = 0.0;
    }

    // Emplace at back of FIFO list
    auto it = level.orders.emplace(level.orders.end(), Order{
        .userId = cmd.userId,
        .userOrderId = cmd.userOrderId,
        .price = cmd.price,
        .remainingQuantity = cmd.quantity
    });
    
    level.totalVolume += cmd.quantity;

    // Direct access mapping
    registry[{cmd.userId, cmd.userOrderId}] = OrderLocation{
        .symbol = symbol_,
        .price = cmd.price,
        .side = side,
        .it = it
    };
}

/**
 * @brief Internal Helper: Maker Removal
 */
template<typename T>
void OrderBook::removeFromSide(T& sideMap, const OrderLocation& loc, const OrderKey& key, 
                               std::unordered_map<OrderKey, OrderLocation>& registry,
                               std::unordered_map<OrderKey, OrderLocation>::iterator regIt) noexcept {
    auto levelIt = sideMap.find(loc.price);
    if (levelIt != sideMap.end()) [[likely]] {
        levelIt->second.totalVolume -= loc.it->remainingQuantity;
        levelIt->second.orders.erase(loc.it);
        if (levelIt->second.orders.empty()) [[unlikely]] {
            sideMap.erase(levelIt);
        }
    }
    outputHandler_.printCancel(key.userId, key.userOrderId);
    registry.erase(regIt);
    checkAndPublishBBO();
}

/**
 * @section EXPLICIT_INSTANTIATIONS
 * Necessary for the Linker to resolve template symbols compiled in this unit.
 */
using BidMap = std::map<double, PriceLevel, std::greater<double>>;
using AskMap = std::map<double, PriceLevel, std::less<double>>;

// Match logic
template void OrderBook::matchAgainstSide<BidMap>(Command&, BidMap&, std::unordered_map<OrderKey, OrderLocation>&) noexcept;
template void OrderBook::matchAgainstSide<AskMap>(Command&, AskMap&, std::unordered_map<OrderKey, OrderLocation>&) noexcept;

// Maker placement logic
template void OrderBook::restOrderOnBook<BidMap>(BidMap&, Command&, Side, std::unordered_map<OrderKey, OrderLocation>&) noexcept;
template void OrderBook::restOrderOnBook<AskMap>(AskMap&, Command&, Side, std::unordered_map<OrderKey, OrderLocation>&) noexcept;

// Removal logic
template void OrderBook::removeFromSide<BidMap>(BidMap&, const OrderLocation&, const OrderKey&, std::unordered_map<OrderKey, OrderLocation>&, std::unordered_map<OrderKey, OrderLocation>::iterator) noexcept;
template void OrderBook::removeFromSide<AskMap>(AskMap&, const OrderLocation&, const OrderKey&, std::unordered_map<OrderKey, OrderLocation>&, std::unordered_map<OrderKey, OrderLocation>::iterator) noexcept;