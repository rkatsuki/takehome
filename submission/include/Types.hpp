#pragma once

#include <string>
#include <vector>
#include <list>
#include <memory>
#include <chrono>
#include <cstdint>
#include <compare>
#include <cstring>
#include <functional>

#include "Constants.hpp"
#include "Precision.hpp"

// --- ID Types ---
// Using fixed-width types ensures the memory footprint is identical across compilers.
using UserID      = uint64_t;
using UserOrderID = uint64_t;
using ExecID      = uint64_t;

// --- Enums ---
// Explicitly setting the underlying type to uint8_t or char keeps our structs small.
enum class CommandType : uint8_t { NEW, CANCEL, FLUSH };
enum class Side : char { BUY = 'B', SELL = 'S' };
enum class OrderStatus : uint8_t { ACTIVE, FILLED, CANCELLED };
enum class OrderType : uint8_t { LIMIT = 0, MARKET = 1 };

/**
 * @brief Fixed-Width Symbol Struct
 * @details In HFT, we avoid std::string to prevent heap allocation and pointer
 * indirection. This 13-byte struct is "Trivially Copyable," meaning the CPU
 * can move it using simple register instructions.
 */
struct Symbol {
    // Value-initialize to zero. This is vital for the bitwise hash to be deterministic.
    char data[Config::SYMBOL_LENGTH + 1] = {0}; 

    Symbol() = default;

    explicit Symbol(std::string_view n) {
        // We limit the copy to SYMBOL_LENGTH to ensure the last byte remains '\0'.
        size_t len = std::min(n.size(), (size_t)Config::SYMBOL_LENGTH);
        std::memcpy(data, n.data(), len);
    }

    // Default comparison performs a fast memcmp-like operation.
    auto operator<=>(const Symbol& other) const = default;
    
    std::string_view view() const {
        return {data, ::strnlen(data, Config::SYMBOL_LENGTH)};
    }
};

// --- OrderBook Internal Structures ---

/**
 * @brief Order Data
 * @details Packed to 40 bytes. This fits 1.5 orders into a single 64-byte 
 * cache line. This layout prioritizes the data most needed during matching.
 */
struct Order {
    UserID userId;           // 8 bytes
    UserOrderID userOrderId;  // 8 bytes
    double price;            // 8 bytes
    double remainingQuantity; // 8 bytes
    uint64_t entryTime;      // 8 bytes (Nanoseconds since epoch)
};

struct PriceLevel {
    double price;            // 8 bytes
    double totalVolume = 0.0; // 8 bytes
    std::list<Order> orders; // Time Priority: FIFO (12-24 byte pointer overhead)
};

/**
 * @brief Order Location Pointer
 * @details This is the value stored in our Registry map. It allows us to 
 * jump directly to an order for cancellation without searching the book.
 */
struct OrderLocation {
    Symbol symbol;                // 13 bytes
    double price;                 // 8 bytes
    Side side;                    // 1 byte
    // 2 bytes of padding usually added here by compiler for pointer alignment
    std::list<Order>::iterator it; // 8-12 bytes (iterator size varies by implementation)
};

// --- Communication Types ---

struct OrderKey {
    UserID userId;
    UserOrderID userOrderId;
    auto operator<=>(const OrderKey&) const = default;
};

/**
 * @brief The Binary Command Struct
 * @details ALIGNAS(64): We force this struct to start at the beginning of a 
 * cache line. At ~45 bytes, the entire command is loaded into the L1 cache 
 * in a single hardware cycle when the Engine receives it.
 */
struct alignas(64) Command {
    CommandType type;      // 1 byte
    OrderType orderType;   // 1 field
    Symbol symbol;         // 13 bytes
    UserID userId;         // 8 bytes (Updated from int for consistency)
    UserOrderID userOrderId; // 8 bytes (Updated from int for consistency)
    double quantity;       // 8 bytes
    double price;          // 8 bytes
    Side side;             // 1 byte
    // Total Size: 48 bytes (+ padding to align to 64 bytes).
};

/**
 * @brief Best Bid/Offer Snapshot
 */
struct BBO {
    double price = -1.0;
    double volume = 0.0;

    // Use epsilon-safe equality to prevent "Ghost BBO" updates due to float jitter.
    bool operator!=(const BBO& other) const {
        return !Precision::isEqual(price, other.price) || 
               !Precision::isEqual(volume, other.volume);
    }
};

// --- Hashing for Unordered Maps ---
namespace std {
    /**
     * @brief Specialized Symbol Hash
     * @details Instead of hashing character by character (slow), we reinterpret 
     * the 12-byte buffer as a 64-bit and 32-bit integer for a fast bitwise hash.
     */
    template<>
    struct hash<Symbol> {
        size_t operator()(const Symbol& s) const noexcept {
            uint64_t first8;
            uint32_t last4;
            std::memcpy(&first8, s.data, 8);
            std::memcpy(&last4, s.data + 8, 4);
            return std::hash<uint64_t>{}(first8) ^ (std::hash<uint32_t>{}(last4) << 1);
        }
    };

    /**
     * @brief Specialized OrderKey Hash
     * @details Uses the "Golden Ratio" hash combine method to ensure that
     * UserID and UserOrderID distribute uniformly across hash buckets.
     */
    template<>
    struct hash<OrderKey> {
        size_t operator()(const OrderKey& k) const noexcept {
            size_t h1 = std::hash<uint64_t>{}(k.userId);
            size_t h2 = std::hash<uint64_t>{}(k.userOrderId);
            // 0x9e3779b9 is the magic constant derived from the Golden Ratio
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };
}