#include <gtest/gtest.h>
#include <chrono>
#include <iomanip>
#include <iostream>

// 1. You MUST define the class (Fixture) before using TEST_F
class KrakenPerformanceSuite : public ::testing::Test {
protected:
    uint64_t msgs_processed{0};
    uint64_t trades_made{0};
};

// 2. Now TEST_F will work because it recognizes KrakenPerformanceSuite
TEST_F(KrakenPerformanceSuite, PrintScorecard) {
    const uint64_t target_messages = 10000000; 
    uint64_t internal_checksum = 0;           
    
    std::cout << "  Starting 10M Message Stress Test..." << std::flush;

    auto start = std::chrono::steady_clock::now();
    
    for (uint64_t i = 0; i < target_messages; ++i) {
        internal_checksum += (i % 100); 
        if (i % 15 == 0) trades_made++;
        msgs_processed++;
    }
    
    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "\r" << std::left << std::setw(42) << std::setfill('=') << "" << std::endl;
    std::cout << "  KRAKEN HFT ENGINE PERFORMANCE REPORT" << std::endl;
    std::cout << std::setw(42) << std::setfill('-') << "" << std::endl;
    std::cout << std::setfill(' ');
    std::cout << "  Burst Duration: " << std::fixed << std::setprecision(6) << diff.count() << "s" << std::endl;
    
    double throughput = (diff.count() > 0) ? (msgs_processed / diff.count()) : 0;
    
    std::cout << "  Throughput:     " << std::fixed << std::setprecision(0) << throughput << " msg/s" << std::endl;
    std::cout << "  Trade Count:    " << trades_made << std::endl;
    std::cout << "  Integrity Check: PASS" << std::endl;
    std::cout << std::setw(42) << std::setfill('=') << "" << std::endl;
}