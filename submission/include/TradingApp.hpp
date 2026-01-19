#pragma once

#include <memory>
#include <thread>

#include "ThreadSafeQueue.hpp"
#include "TradingEngine.hpp"
#include "CSVParser.hpp"
#include "UDPServer.hpp"

#pragma once

#include <memory>
#include <thread>
#include <string>
#include <atomic>

#include "ThreadSafeQueue.hpp"
#include "OutputHandler.hpp"
#include "TradingEngine.hpp"
#include "CSVParser.hpp"
#include "UDPServer.hpp"

class TradingApp {
    // Allows the test suite to inspect queues directly without public getters
    friend class TradingAppE2ESuite;

public:
    TradingApp();
    ~TradingApp();

    void run();        // The main entry point (Pillar 3)
    void stop();       // The "Shutdown" (Cleans up all pillars)
    void flushState(); // The "Reset" (Clears engine and pipes)

private:
    // Foundation: Shared queues for inter-thread communication
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> outputQueue_;
    std::shared_ptr<ThreadSafeQueue<std::string>> inputQueue_;

    // Pillar 1: The Output Gateway
    OutputHandler outputHandler_; 

    // Pillar 2: The Logic Engine & Parser
    TradingEngine engine_;
    CSVParser parser_;

    // Pillar 3: The Network Gateway
    std::unique_ptr<UDPServer> server_;
    
    // Execution Threads
    std::thread processingThread_;
    std::thread outputThread_;
};