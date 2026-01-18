#include "TradingApp.hpp"

#include <csignal>
#include <iostream>

std::atomic<bool> keepRunning{true};

// Internal signal handler for this translation unit
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        keepRunning = false;
    }
}

/**
 * @brief System Constructor (The Wiring Phase)
 * @details We use a "Dependency Injection" pattern here. By passing references 
 * to the OutputHandler, we ensure that the Parser and Engine can communicate 
 * back to the user without knowing the details of the ThreadSafeQueue.
 */
TradingApp::TradingApp() 
    : outputQueue_(std::make_shared<ThreadSafeQueue<OutputEnvelope>>()),
      inputQueue_(std::make_shared<ThreadSafeQueue<std::string>>()),
      // 1. Initialize the handler with the queue
      outputHandler_(outputQueue_), 
      // 2. Pass the SAME handler to both the engine and parser
      engine_(outputHandler_),
      parser_(outputHandler_), 
      server_(nullptr)
    {
    /**
     * @note The Network Callback:
     * This lambda is the bridge between the UDP thread and the Processing thread.
     * It performs a simple 'push', keeping the network thread unblocked.
     */
    server_ = std::make_unique<UDPServer>(Config::UDP_PORT, [this](const std::string& raw) {
        inputQueue_->push(raw);
    });

    std::cerr << "APP_INIT: 3-Pillar Architecture Wired (Input -> Logic -> Output)." << std::endl;
}

/**
 * @brief The Main Execution Loop
 * @details Orchestrates the lifecycle of the three primary execution pillars.
 */
void TradingApp::run() {
    std::signal(SIGINT, signalHandler);

    /**
     * @pillar PILLAR 1: THE OUTPUT TAPE (I/O Bound)
     * @details Uses the "Batch Swap" optimization. Instead of waking up for 
     * every single trade, it sleeps until work is available and then 
     * flushes everything in a local burst, minimizing syscall overhead.
     */
    outputThread_ = std::thread([this]() {
        std::queue<OutputEnvelope> localBatch;
        
        while (outputQueue_->pop_all(localBatch)) [[likely]] {
            while (!localBatch.empty()) {
                const auto& env = localBatch.front();
                
                if (env.type == MsgType::Data) [[likely]] {
                    // Use write() + length for zero-allocation printing
                    std::cout.write(env.buffer.data(), env.length);
                } else {
                    std::cerr.write(env.buffer.data(), env.length);
                }
                localBatch.pop();
            }
            // Flush once per batch to keep the terminal responsive but efficient
            std::cout.flush();
        }
    });

    /**
     * @pillar PILLAR 2: THE LOGIC ENGINE (CPU Bound)
     * @details This is the "Single Writer." It serializes all commands 
     * from the network into a deterministic sequence of book updates.
     * keepRunning is an extern in header file, initialized and controlled by the handle_signal defined at the top of the file.
     */
    processingThread_ = std::thread([this]() {
        while (keepRunning) [[likely]] {
            auto raw = inputQueue_->pop();
            
            if (raw) [[likely]] {
                // Pass the raw string to the Zero-Allocation parser
                parser_.parseAndExecute(*raw, engine_);
            } else {
                break; // Queue was stopped
            }
        }
    });

    /**
     * @pillar PILLAR 3: THE INPUT GATE (Network Bound)
     * @details Starts the UDP receiver loops to begin filling the inputQueue_.
     */
    server_->start();
    
    std::cerr << "SYSTEM_READY: Processing trades on port " << Config::UDP_PORT << std::endl;

    // Main thread enters a low-power wait state until Ctrl+C
    while (keepRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    /**
     * @section GRACEFUL SHUTDOWN (The Domino Effect)
     * To ensure no data is lost, we must stop the pillars in order:
     * 1. Stop Network (No more new data)
     * 2. Stop Input Queue (Drain the Parser)
     * 3. Stop Output Queue (Finalize the Tape)
     */
    std::cerr << "\nSHUTTING_DOWN: Draining queues..." << std::endl;
    
    server_->stop();        // Step 1: Kill network threads
    inputQueue_->stop();    // Step 2: Signal Parser thread to finish
    
    if (processingThread_.joinable()) {
        processingThread_.join();
    }

    outputQueue_->stop();   // Step 3: Signal Output thread to finish last trades
    
    if (outputThread_.joinable()) {
        outputThread_.join();
    }
    
    std::cerr << "SHUTDOWN_COMPLETE: Core Engine halted." << std::endl;
}