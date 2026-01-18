#include <gtest/gtest.h>
#include <arpa/inet.h>
#include <atomic>
#include <thread>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <queue>

#include "TradingApp.hpp"

extern std::atomic<bool> keepRunning;

class TradingAppE2ESuite : public ::testing::Test {
protected:
    static std::unique_ptr<TradingApp> app;
    static std::thread appThread;
    static int client_fd;
    static sockaddr_in server_addr;

    // --- Life Cycle Management ---

static void SetUpTestSuite() {
    keepRunning = true;
    
    // 1. Create the instance first
    auto instance = std::make_unique<TradingApp>();
    TradingApp* rawPtr = instance.get();
    
    // 2. Transfer ownership to the static member
    app = std::move(instance);

    // 3. Start the thread using the RAW POINTER we just verified
    // This avoids the thread trying to access the static 'app' variable 
    // which might not be updated in the thread's cache yet.
    appThread = std::thread([rawPtr]() {
        if (rawPtr) {
            rawPtr->run();
        }
    });

    // 4. Setup UDP Client (Existing code)
    client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    // Give the threads a moment to bind the socket and start the loops
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}

    static void TearDownTestSuite() {
        keepRunning = false;
        if (appThread.joinable()) appThread.join();
        app.reset();
        if (client_fd >= 0) close(client_fd);
    }

    void TearDown() override {
        if (app) {
            app->flushState(); // Ensures clean state for the next TEST_F
        }
    }

    // --- Utilities & DSL Helpers ---

    void sendMsg(const std::string& msg) {
        ASSERT_GT(client_fd, 0) << "Socket was never opened!";
        ssize_t sent = sendto(client_fd, msg.c_str(), msg.size(), 0, 
                            (struct sockaddr*)&server_addr, sizeof(server_addr));
        ASSERT_GT(sent, 0) << "Failed to send packet: " << strerror(errno);
    }

    std::vector<std::string> split(const std::string& s) {
        std::vector<std::string> fields;
        std::stringstream ss(s);
        std::string field;
        while (std::getline(ss, field, ',')) {
            field.erase(field.find_last_not_of(" \n\r\t") + 1);
            fields.push_back(field);
        }
        return fields;
    }


    std::string getNextOutput(int timeoutMs = 500) {
        if (!app || !app->outputQueue_) return "ERROR_APP_NULL";
        
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            
            // Use the new non-blocking call
            auto optEnv = app->outputQueue_->try_pop(); 
            
            if (optEnv.has_value()) {
                const auto& env = *optEnv;
                if (env.length > 0) {
                    std::string s(env.buffer.data(), env.length);
                    
                    // Remove trailing newline for char-by-char comparison
                    size_t last = s.find_last_not_of(" \n\r\t");
                    if (last != std::string::npos) s.erase(last + 1);
                    
                    return s;
                }
            }
            // Poll every 1ms. Fast enough for HFT tests, but saves CPU.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return "TIMEOUT_NO_MSG"; 
    }

    // --- Custom Assertions ---

    void EXPECT_ACK(const std::string& userId, const std::string& orderId) {
        std::string actual = getNextOutput();
        // Reviewer format: "A, <userId>, <orderId>"
        std::string expected = std::format("A, {}, {}", userId, orderId);
        
        // Strict char-by-char string comparison
        ASSERT_EQ(actual, expected) << "ACK Protocol Mismatch!";
    }

    void EXPECT_CANCEL(const std::string& userId, const std::string& orderId) {
        std::string actual = getNextOutput();
        std::string expected = std::format("C, {}, {}", userId, orderId);
        
        ASSERT_EQ(actual, expected) << "CANCEL Protocol Mismatch!";
    }

    void EXPECT_TRADE(const std::string& bId, const std::string& bOrd, 
                    const std::string& sId, const std::string& sOrd, 
                    const std::string& p, const std::string& q) {
        std::string actual = getNextOutput();
        // Reviewer format: "T, <bId>, <bOrd>, <sId>, <sOrd>, <p>, <q>"
        std::string expected = std::format("T, {}, {}, {}, {}, {}, {}", bId, bOrd, sId, sOrd, p, q);
        
        ASSERT_EQ(actual, expected) << "TRADE Protocol Mismatch!";
    }

    void EXPECT_BOOK(const std::string& side, const std::string& price, const std::string& qty) {
        std::string actual = getNextOutput();
        // Reviewer format: "B, <side>, <price>, <qty>"
        std::string expected = std::format("B, {}, {}, {}", side, price, qty);
        
        ASSERT_EQ(actual, expected) << "BBO Protocol Mismatch!";
    }

};

// Static Initializations
std::unique_ptr<TradingApp> TradingAppE2ESuite::app = nullptr;
std::thread TradingAppE2ESuite::appThread;
int TradingAppE2ESuite::client_fd = -1;
sockaddr_in TradingAppE2ESuite::server_addr{};

// --- Test Scenarios ---

TEST_F(TradingAppE2ESuite, PriceTimePriority_FirstInFirstFilled) {
    // User 1 & 2 resting at 50k. User 3 sells into them.
    sendMsg("N,BTC/USD,1,101,B,5.0,50000.0");
    EXPECT_ACK("1", "101");
    EXPECT_BOOK("B", "50000.0", "5.0");

    sendMsg("N,BTC/USD,2,201,B,5.0,50000.0");
    EXPECT_ACK("2", "201");
    EXPECT_BOOK("B", "50000.0", "10.0");

    sendMsg("N,BTC/USD,3,301,S,7.0,50000.0");
    EXPECT_ACK("3", "301");

    // Validating Priority
    EXPECT_TRADE("1", "101", "3", "301", "50000.0", "5.0");
    EXPECT_TRADE("2", "201", "3", "301", "50000.0", "2.0");
    EXPECT_BOOK("B", "50000.0", "3.0"); // 10.0 - 7.0 = 3.0 remaining
}

TEST_F(TradingAppE2ESuite, Cancel_RemovesLiquidity) {
    sendMsg("N,BTC/USD,1,101,B,10.0,50000.0");
    EXPECT_ACK("1", "101");
    EXPECT_BOOK("B", "50000.0", "10.0");

    // Cancel order
    sendMsg("C,BTC/USD,1,101");
    EXPECT_CANCEL("1", "101");
    EXPECT_BOOK("B", "-", "-"); // Side eliminated
}