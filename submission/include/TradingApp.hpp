#pragma once

#include <memory>
#include <thread>

#include "ThreadSafeQueue.hpp"
#include "TradingEngine.hpp"
#include "CSVParser.hpp"
#include "UDPServer.hpp"

class TradingApp {
public:
    TradingApp();
    void run();

private:
    // Foundation: Queues must exist first
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> outputQueue_;
    std::shared_ptr<ThreadSafeQueue<std::string>> inputQueue_;

    // Pillar 1: The shared OutputHandler (owned by App)
    OutputHandler outputHandler_; 

    // Pillar 2: The Logic and Parser (Both hold a reference to outputHandler_)
    TradingEngine engine_;
    CSVParser parser_;

    // Pillar 3: The Network Gateway
    std::unique_ptr<UDPServer> server_;
    
    // Threads
    std::thread processingThread_;
    std::thread outputThread_;
};