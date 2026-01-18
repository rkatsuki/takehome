#pragma once

#include <unordered_map>
#include <memory>

#include "OrderBook.hpp"

class TradingEngine {
    friend class TradingApp;
public:
    explicit TradingEngine(OutputHandler& handler);

    ~TradingEngine() = default;

    TradingEngine(const TradingEngine&) = delete;
    TradingEngine& operator=(const TradingEngine&) = delete;

    void processCommand(Command& cmd);
    // Logic for the 'F' command
    void handleFlush();

private:
    // Reference to the handler that pushes to the Output Queue
    OutputHandler& outputHandler_;

    std::unordered_map<Symbol, std::unique_ptr<OrderBook>> books_;
    std::unordered_map<OrderKey, OrderLocation> registry_;

    // No more static mutex! The OutputThread handles safety.

    OrderBook& getOrCreateBook(Symbol symbol);
    
    bool canAcceptNewOrder() const;
    bool validateOrder(const Command& cmd, std::string& outError);
};