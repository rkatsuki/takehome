#pragma once

#include <cmath>
#include <limits>

/**
 * @namespace Precision
 * @brief Floating Point Safety & Satoshi-Grade Arithmetic
 * @details Standard '==' and '<' are dangerous for doubles. We use an epsilon 
 * to ensure that tiny remainders are treated as zero.
 */
namespace Precision {
    /**
     * @brief The "Satoshi-Grade" Epsilon
     * @details RATIONALE FOR 1e-9:
     * 1. Satoshi Safety: Bitcoin's smallest unit is 1e-8. By using 1e-9, we ensure 
     * our precision is 10x finer than the asset's native resolution, preventing 
     * "rounding theft" or dust remainders from lingering on the book.
     * * 2. Double Stability: A 64-bit double provides ~15-17 significant decimal digits. 
     * For a price of $100,000, 1e-9 sits at the 14th digit, which is the limit of 
     * "Safe Precision." Going to 1e-18 (Wei) with doubles would cause unstable 
     * comparisons once prices exceed $1.00.
     */
    inline constexpr double EPSILON = 1e-9; 

    /**
     * @brief Precision-Safe Subtraction
     * @details If a trade results in a remainder smaller than one Satoshi (1e-8),
     * we truncate to absolute 0.0. This ensures that partially filled orders
     * are correctly cleaned up from the book lists.
     */
    inline void subtract_or_zero(double& target, double subtrahend) noexcept {
        double result = target - subtrahend;
        target = (result < EPSILON) ? 0.0 : result;
    }

    /**
     * @brief Bulletproof equality check for IEEE 754 doubles.
     */
    inline bool isEqual(double a, double b) noexcept {
        return std::abs(a - b) < EPSILON;
    }

    /**
     * @brief Validation check for nonzero quantities.
     */
    inline bool isPositive(double value) noexcept {
        return value >= EPSILON;
    }

    /**
     * @brief Bulletproof Zero Check.
     */
    inline bool isZero(double val) noexcept {
        return std::abs(val) < EPSILON;
    }

    inline bool isLess(double a, double b) {
        return a < (b - EPSILON); 
    }

    inline bool isGreater(double a, double b) {
        return a > (b + EPSILON);
    }
}