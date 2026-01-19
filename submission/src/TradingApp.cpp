#include "TradingApp.hpp"
#include <csignal>
#include <iostream>

std::atomic<bool> keepRunning{true};

void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        keepRunning = false;
    }
}

TradingApp::TradingApp() 
    : outputQueue_(std::make_shared<ThreadSafeQueue<OutputEnvelope>>()),
      inputQueue_(std::make_shared<ThreadSafeQueue<std::string>>()),
      outputHandler_(outputQueue_), 
      engine_(outputHandler_),
      parser_(outputHandler_)
{
    server_ = std::make_unique<UDPServer>(inputQueue_);
}

TradingApp::~TradingApp() {
    stop(); 
}

void TradingApp::stop() {
    keepRunning = false; 

    if (server_) server_->stop();
    if (inputQueue_) inputQueue_->stop();
    if (outputQueue_) outputQueue_->stop();

    if (processingThread_.joinable()) processingThread_.join();
    if (outputThread_.joinable()) outputThread_.join();
}

void TradingApp::run() {
    std::signal(SIGINT, signalHandler);

    // Pillar 1: Output Tape (Batch processing remains efficient)
    outputThread_ = std::thread([this]() {
        std::queue<OutputEnvelope> localBatch;
        while (outputQueue_->pop_all(localBatch)) {
            if (localBatch.empty()) {
                // This happens if pop_all was unblocked by a stop signal
                continue; 
            }

            while (!localBatch.empty()) {
                const auto& env = localBatch.front();
                
                // Choose stream and write
                if (env.type == MsgType::Data) {
                    std::cout.write(env.buffer.data(), env.length);
                } else {
                    std::cerr.write(env.buffer.data(), env.length);
                }
                localBatch.pop();
            }
            
            // CRITICAL: Force the OS to flush the pipe immediately
            std::cout.flush();
            std::cerr.flush();
            
            // Give the OS a microsecond to actually draw to the terminal
            std::this_thread::yield(); 
        }
    });

    /**
     * @pillar PILLAR 2: THE LOGIC ENGINE (Hybrid Spin-Yield)
     * Strategy: Pure Non-Blocking. Spin with pause hints, then yield.
     */
    processingThread_ = std::thread([this]() {
        const uint32_t SPIN_THRESHOLD = 1000000; // Adjust based on your core speed
        uint32_t emptyCycles = 0;

        while (keepRunning) [[likely]] {
            auto raw = inputQueue_->try_pop();
            
            if (raw) [[likely]] {
                parser_.parseAndExecute(*raw, engine_);
                emptyCycles = 0; // Reset as soon as work is found
            } else {
                emptyCycles++;
                if (emptyCycles < SPIN_THRESHOLD) {
                    // Phase 1: Aggressive Spin with hardware hint
                    asm volatile("pause" ::: "memory");
                } else {
                    // Phase 2: Give up CPU slice to other threads (Network/Output)
                    std::this_thread::yield();
                    // Optional: clamp emptyCycles to keep it in the yield phase
                    emptyCycles = SPIN_THRESHOLD; 
                }
            }
        }
    });

    // Pillar 3: Network
    server_->start();
    
    while (keepRunning) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    stop(); 
}

void TradingApp::flushState() {
    engine_.handleFlush(); 
    
    std::queue<std::string> dummyIn;
    inputQueue_->pop_all(dummyIn);

    std::queue<OutputEnvelope> dummyOut;
    outputQueue_->pop_all(dummyOut);
}