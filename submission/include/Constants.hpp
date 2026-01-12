#pragma once
#include <set>
#include <string>

// ====================================================================
// Global Engine Configuration & Resource Guardrails
// ====================================================================
namespace Config {
    // 1. Traded Symbols
    const std::set<std::string> TRADED_SYMBOLS = {
        "BTC/USD", 
        "ETH/USD", 
        "SOL/USD", 
        "XRP/USD"
    };

    // 2. Engine-Wide Limits
    inline constexpr int  ID_SHARD_COUNT      = 16;         // Number of mutex-protected ID shards; assunmptions 16-32 cores
    inline constexpr long MAX_GLOBAL_ORDERS   = 10'000'000; // Hard cap on total orders in RAM; expect to use upto 2BM RAM and no disk swap space; OrderEntry and its lists and maps is about 150–250 bytes per order. 10M times 200 bytes = 2 GB

    // 3. Per-OrderBook Limits (Resource Protection)
    inline constexpr long MAX_ORDERS_PER_BOOK = 1'000'000;  // Prevents one symbol from eating all RAM; ensure not all RAM is used up by the most actively traded symbol
    inline constexpr int  MAX_PRICE_LEVELS    = 20'000;     // Prevents "Quote Stuffing" fragmenting the map; the limit keeps the time it takes to find a price -- O(log N) -- performant.
    inline constexpr int  MAX_TAG_SIZE        = 64;         // Max bytes for user-provided string tags; Memory fragmentation and Small String Optimization

    // 4. Validation Limits (Trading Rules)
    inline constexpr long   MAX_ORDER_QTY     = 1'000'000'000; // "Fat Finger" protection
    inline constexpr double MIN_ORDER_PRICE   = 0.00000001;    // Minimum tick size; Standard Satoshi-level precision.
    inline constexpr double MAX_ORDER_PRICE   = 1'000'000'000.0;
    inline constexpr double PRICE_BAND_PERCENT = 1.0;            // Limits the resting orders and clutter in Orderbook
}