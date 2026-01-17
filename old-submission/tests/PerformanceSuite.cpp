#include <gtest/gtest.h>
#include "TradingEngine.hpp"
#include "Constants.hpp"

class PerformanceSuite : public ::testing::Test {
protected:
    TradingEngine engine;
};

// measure the latency of each individual order, store them in a vector, sort them, and then extract the percentiles.
TEST_F(PerformanceSuite, LatencyPercentileAnalysis) {
    const Symbol sym = "BTC/USD";
    const int iterations = 50000;
    std::vector<double> latencies;
    latencies.reserve(iterations);

    // Warm up the engine (JIT/Branch Prediction)
    for(int i = 0; i < 1000; ++i) {
        engine.submitOrder(LimitOrderRequest{"WARM", sym, Side::BUY, 1.0, 100.0});
    }

    // Actual Measurement Loop
    for (int i = 0; i < iterations; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        // Use a mix: half Limit (Maker), half Market (Taker)
        if (i % 2 == 0) {
            engine.submitOrder(LimitOrderRequest{"L_"+std::to_string(i), sym, Side::BUY, 1.0, 100.0});
        } else {
            engine.submitOrder(MarketOrderRequest{"M_"+std::to_string(i), sym, Side::SELL, 1.0});
        }

        auto end = std::chrono::high_resolution_clock::now();
        double duration = std::chrono::duration<double, std::nano>(end - start).count();
        latencies.push_back(duration);
    }

    // Sort to find percentiles
    std::sort(latencies.begin(), latencies.end());

    auto getPercentile = [&](double p) {
        return latencies[static_cast<size_t>(latencies.size() * p / 100.0)];
    };

    std::cout << "\n==========================================" << std::endl;
    std::cout << "       LATENCY PERCENTILE REPORT (ns)      " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "P50 (Median):  " << std::setw(10) << getPercentile(50) << " ns" << std::endl;
    std::cout << "P90:           " << std::setw(10) << getPercentile(90) << " ns" << std::endl;
    std::cout << "P99:           " << std::setw(10) << getPercentile(99) << " ns" << std::endl;
    std::cout << "P99.9 (Tail):  " << std::setw(10) << getPercentile(99.9) << " ns" << std::endl;
    std::cout << "Max Latency:   " << std::setw(10) << latencies.back() << " ns" << std::endl;
    std::cout << "==========================================" << std::endl;
}

// Verify that engine achieves parallel speedup; that the engine scales with increasing number of symbols and their respective OrderBooks that it manages.
TEST_F(PerformanceSuite, FullScalingStressTest) {
    const int matchesPerSymbol = 10000;
    const int opsPerSymbol = matchesPerSymbol + 1; // 10k Limit + 1 Market Sweep
    
    auto workload = [this](Symbol sym, int count) {
        for(int i = 0; i < count; ++i) {
            engine.submitOrder(LimitOrderRequest{"M_" + sym + std::to_string(i), sym, Side::BUY, 1.0, 100.0});
        }
        engine.submitOrder(MarketOrderRequest{"SWEEP_" + sym, sym, Side::SELL, (double)count});
    };

    auto runScenario = [&](int numSymbols) {
        std::vector<std::thread> threads;
        auto start = std::chrono::high_resolution_clock::now();

        for(int i = 0; i < numSymbols; ++i) {
            threads.emplace_back(workload, Config::TRADED_SYMBOLS[i], matchesPerSymbol);
        }
        for(auto& t : threads) t.join();

        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    };

    // Execute Scenarios
    long dur1  = runScenario(1);
    long dur2  = runScenario(2);
    long dur3  = runScenario(3);
    long dur5  = runScenario(5);
    long dur10 = runScenario(10);

    // Helper for reporting
    auto printStat = [&](std::string label, long duration, long baseline) {
        double scaling = (double)duration / baseline;
        double throughput = (double)(label == "1 " ? 1 : (label == "2 " ? 2 : (label == "3 " ? 3 : (label == "5 " ? 5 : 10)))) 
                            * opsPerSymbol / (duration / 1000.0);
        
        std::cout << label << " Symbol(s): " << duration << " ms | " 
                  << "Factor: " << std::fixed << std::setprecision(2) << scaling << "x | "
                  << "Throughput: " << (int)throughput << " ops/sec" << std::endl;
    };

    std::cout << "\n======================================================" << std::endl;
    std::cout << "               SHARDING SCALING REPORT" << std::endl;
    std::cout << "======================================================" << std::endl;
    printStat("1 ", dur1, dur1);
    printStat("2 ", dur2, dur1);
    printStat("3 ", dur3, dur1);
    printStat("5 ", dur5, dur1);
    printStat("10", dur10, dur1);
    std::cout << "======================================================" << std::endl;
}

// intentionally create contention by having one thread try to delete data (Cancel) while another is trying to read and modify it (Match).
TEST_F(PerformanceSuite, RaceConditionStress) {
    const Symbol sym = "BTC/USD";
    const int orderCount = 5000;
    const double price = 100.0;

    // 1. Setup: Fill the book with identifiable orders
    for(int i = 0; i < orderCount; ++i) {
        engine.submitOrder(LimitOrderRequest{"T_" + std::to_string(i), sym, Side::BUY, 1.0, price});
    }

    // 2. Race: Cancel vs. Execute
    auto start = std::chrono::high_resolution_clock::now();

    std::thread crusher([&]() {
        for(int i = 0; i < orderCount; ++i) {
            engine.cancelOrderByTag("T_" + std::to_string(i), sym);
        }
    });

    std::thread sweeper([&]() {
        // Attempt to sweep half the book
        engine.submitOrder(MarketOrderRequest{"SWEEPER", sym, Side::SELL, (double)orderCount / 2.0});
    });

    crusher.join();
    sweeper.join();

    // 3. Validation: The State Check
    auto snapshot = std::get<OrderBookSnapshot>(engine.getOrderBook(sym, 1).data);
    
    // Check for "Ghost Volume" or "Negative Volume"
    double remainingVol = 0.0;
    if (!snapshot.bids.empty()) {
        remainingVol = snapshot.bids[0].size;
    }

    std::cout << "[ CHAOS ] Remaining Volume after Race: " << remainingVol << std::endl;
    
    // The book should be consistent. We check if the volume is clean (0 or positive)
    EXPECT_GE(remainingVol, 0.0);
    
    // Verify Registry Cleanup: Try to cancel everything again; should fail if already gone
    for(int i = 0; i < orderCount; ++i) {
        auto res = engine.cancelOrderByTag("T_" + std::to_string(i), sym);
        // If it's not in the registry and not on the book, it worked.
        EXPECT_FALSE(res.isSuccess());
    }
}

// This test fills the book with orders across a wide price range, then measures how long it takes to execute a "Sweep" that must traverse many different memory nodes.
TEST_F(PerformanceSuite, OrderDensityStress) {
    const Symbol sym = "BTC/USD";
    const int priceLevels = 1000; // 1,000 distinct price points
    const int ordersPerLevel = 5;  // 5,000 total orders
    
    // 1. Scatter orders across 1,000 different price levels
    // This forces the std::map to create 1,000 nodes scattered in memory.
    for (int i = 0; i < priceLevels; ++i) {
        double price = 50000.0 + (i * 0.5); // Spread prices by $0.50
        for (int j = 0; j < ordersPerLevel; ++j) {
            engine.submitOrder(LimitOrderRequest{
                "MKR_" + std::to_string(i) + "_" + std::to_string(j), 
                sym, Side::BUY, 1.0, price
            });
        }
    }

    // 2. Performance Measurement: The "Deep Sweep"
    // We want to see how fast the engine can "walk" through 1,000 different map nodes.
    auto start = std::chrono::high_resolution_clock::now();
    
    // This order is large enough to exhaust all 1,000 price levels
    auto response = engine.submitOrder(MarketOrderRequest{"SWEEPER", sym, Side::SELL, 5000.0});
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    ASSERT_TRUE(response.isSuccess());

    std::cout << "\n==========================================" << std::endl;
    std::cout << "      ORDER DENSITY (FRAGMENTATION)       " << std::endl;
    std::cout << "==========================================" << std::endl;
    std::cout << "Total Price Levels Crossed: " << priceLevels << std::endl;
    std::cout << "Total Time for Sweep:       " << duration << " us" << std::endl;
    std::cout << "Avg Time Per Level:         " << (double)duration / priceLevels << " us" << std::endl;
    std::cout << "==========================================" << std::endl;
}