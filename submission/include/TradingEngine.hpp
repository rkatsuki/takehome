#pragma once

#include <unordered_map>
#include <memory>

#include "OrderBook.hpp"

class TradingEngine {
public:
    explicit TradingEngine(OutputHandler& handler);

    ~TradingEngine() = default;

    TradingEngine(const TradingEngine&) = delete;
    TradingEngine& operator=(const TradingEngine&) = delete;

    void processCommand(Command& cmd);
private:
    // Reference to the handler that pushes to the Output Queue
    OutputHandler& outputHandler_;

    std::unordered_map<Symbol, std::unique_ptr<OrderBook>> books_;
    std::unordered_map<OrderKey, OrderLocation> registry_;

    // No more static mutex! The OutputThread handles safety.

    OrderBook& getOrCreateBook(Symbol symbol);
    
    // Logic for the 'F' command
    void handleFlush();

    bool canAcceptNewOrder() const;
    bool validateOrder(const Command& cmd, std::string& outError);
};