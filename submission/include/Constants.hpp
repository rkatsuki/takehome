#pragma once

#include <string>
#include <vector>
#include <ranges>

/**
 * @namespace Config
 * @brief Global Engine Configuration & Architectural Guardrails
 * * @details 
 * DESIGN PHILOSOPHY:
 * 1. Mechanical Sympathy: Constants are tuned to fit within L3 cache boundaries 
 * to minimize DRAM stalls.
 * 2. Numerical Stability: Boundaries enforce IEEE 754 double-precision safety 
 * to prevent catastrophic cancellation in 1e-9 (Satoshi) math.
 * 3. Determinism: Resource limits prevent "Rehash Storms" and OOM kills in 
 * Dockerized environments.
 */
namespace Config {
    inline constexpr bool DEBUG = false;    

    // --- SYMBOL CONFIGURATION ---
    
    /**
     * @note ARCHITECTURAL DECISION: Fixed-width capacity.
     * Keeps the Command struct 'TriviallyCopyable' (POD). This allows the CPU to 
     * use SIMD registers for data movement and avoids heap allocations 
     * during UDP packet ingestion.
     */
    inline constexpr int SYMBOL_LENGTH = 12; 

    const std::vector<std::string> TRADED_SYMBOLS = {
        "IBM", "APPL", "MSFT", "VAL",
        "BTC/USD", "BTC/USDT", "BTC/USDC", "ETH/BTC", 
        "ETH/USD", "ETH/USDT", "ETH/USDC", 
        "SOL/USD", "ADA/USD", "DOT/USD", "AVAX/USD", "MATIC/USD", "LINK/USD", "UNI/USD", "LTC/USD"
    };

    /**
     * @brief O(N) check is acceptable as it only runs once per NEW order.
     * Branch-predictor friendly iteration via std::ranges.
     */
    inline bool isSupported(std::string_view symbol) {
        return std::ranges::find(TRADED_SYMBOLS, symbol) != TRADED_SYMBOLS.end();
    }

    // --- SYSTEM RESOURCE GUARDRAILS ---

    /**
     * @note ARCHITECTURAL DECISION: L3 Cache Optimization.
     * 1M orders @ ~128 bytes each keeps the primary hash-map 'spine' (bucket array) 
     * within typical 16-32MB L3 caches, ensuring O(1) lookups stay in tens of nanoseconds.
     */
    inline constexpr long MAX_GLOBAL_ORDERS = 1'000'000; 
    
    /**
     * @note ARCHITECTURAL DECISION: Search Performance Protection.
     * Limits the 'depth' of the Price Level map (Red-Black Tree). This prevents 
     * 'Price Spray' attacks that would degrade O(log N) search performance 
     * and cause cache thrashing.
     */
    inline constexpr int  MAX_PRICE_LEVELS    = 20'000;     

    // --- ARITHMETIC BOUNDARIES (PRECISION SAFETY) ---

    /**
     * @note ARCHITECTURAL DECISION: The "Mantissa Wall" (1e9 / 1e-9 Sandwich).
     * IEEE 754 doubles provide ~15.9 decimal digits of precision. By capping 
     * Qty at 10^9 and Min Qty at 10^-9, we ensure that adding a 'Satoshi' 
     * to a max-size order doesn't result in loss of significance.
     */
    inline constexpr double MIN_ORDER_QTY     = 0.000000001; // 1e-9 (Satoshi-grade)
    inline constexpr long   MAX_ORDER_QTY     = 1'000'000'000;

    inline constexpr double MIN_ORDER_PRICE   = 0.00000001;    
    inline constexpr double MAX_ORDER_PRICE   = 1'000'000'000.0;
    
    // --- VOLATILITY GUARDRAILS ---

    /**
     * @note ARCHITECTURAL DECISION: Dynamic Price Corridor.
     * In the absence of a fixed Tick Size, this 50% threshold is the primary 
     * defense against 'fat-finger' errors and market manipulation. 
     * Protects the integrity of the Last Traded Price (LTP).
     */
    inline constexpr double PRICE_CORRIDOR_THRESHOLD = 1;

    // --- NETWORK CONFIGURATION ---
    struct Network {
        static inline const std::string SERVER_IP = "127.0.0.1"; 
        static inline constexpr int UDP_PORT = 1234;
        
        // 8MB kernel buffer to survive market data bursts
        static inline constexpr int SO_RCVBUF_SIZE = 8 * 1024 * 1024; 
        static inline constexpr size_t MAX_PACKET_SIZE = 4096;
    };    
}