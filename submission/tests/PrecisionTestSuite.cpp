#include <gtest/gtest.h>
#include "Precision.hpp"
#include "Constants.hpp"

/**
 * @brief Test Suite for Floating Point Math
 * @details In HFT, we use Epsilon-safe comparisons to prevent 
 * "Price Improvement" or "Ghost Orders" caused by binary rounding.
 */
class PrecisionTestSuite : public ::testing::Test {
    // Fixture for future setup/teardown if needed
};

TEST_F(PrecisionTestSuite, EpsilonEquality) {
    double val = 1.0;
    // This value is mathematically different but should be logically equal
    double almostVal = 1.0 + (Precision::EPSILON / 2.0);
    
    EXPECT_TRUE(Precision::isEqual(val, almostVal));
    
    // Test inequality outside the epsilon range
    double differentVal = 1.0 + (Precision::EPSILON * 2.0);
    EXPECT_FALSE(Precision::isEqual(val, differentVal));
}

TEST_F(PrecisionTestSuite, ZeroAndDustHandling) {
    // "Dust" refers to quantities too small to be economically relevant
    double dust = Precision::EPSILON / 10.0;
    EXPECT_TRUE(Precision::isZero(dust));
    EXPECT_TRUE(Precision::isZero(-dust));
    
    // Verify that the smallest allowed order is actually greater than zero
    EXPECT_FALSE(Precision::isZero(Config::MIN_ORDER_QTY));
}

TEST_F(PrecisionTestSuite, ComparativeLogic) {
    double small = 0.000000001; // 1e-9
    double large = 0.000000002; // 2e-9
    
    // Logic: isPositive means > EPSILON
    EXPECT_TRUE(Precision::isPositive(large));
    
    // Basic order book crossing math: (Price A >= Price B)
    double bid = 100.0000000001;
    double ask = 100.0000000002;
    EXPECT_TRUE(Precision::isEqual(bid, ask));
}

// Key test to keep the integrity of size and price stored as double
TEST_F(PrecisionTestSuite, ConfigConstantsSanity) {
    // MIN_ORDER_QTY can be set to close to zero but not below EPSILON
    EXPECT_TRUE(Precision::isPositive(Config::MIN_ORDER_QTY));
    // MIN_ORDER_PRICE can be set to close to zero but not below EPSILON
    EXPECT_TRUE(Precision::isPositive(Config::MIN_ORDER_PRICE));
}

/**
 * @brief Tests the floating-point robustness of the engine.
 * We use 1e-9 as our EPSILON.
 */
TEST_F(PrecisionTestSuite, EpsilonComparisons) {
    double base = 100.0;
    double tiny_bit_more = 100.0 + (Precision::EPSILON / 2.0);
    double tiny_bit_less = 100.0 - (Precision::EPSILON / 2.0);
    double significant_more = 100.0 + (Precision::EPSILON * 2.0);
    double significant_less = 100.0 - (Precision::EPSILON * 2.0);

    // 1. Equality: Within half-epsilon should be "Equal"
    EXPECT_TRUE(Precision::isEqual(base, tiny_bit_more));
    EXPECT_TRUE(Precision::isEqual(base, tiny_bit_less));
    
    // 2. Inequality: Beyond epsilon should NOT be "Equal"
    EXPECT_FALSE(Precision::isEqual(base, significant_more));

    // 3. isGreater logic: 
    // base + 0.5*EPSILON is NOT greater than base (it's within the noise floor)
    EXPECT_FALSE(Precision::isGreater(tiny_bit_more, base));
    // base + 2.0*EPSILON IS definitely greater than base
    EXPECT_TRUE(Precision::isGreater(significant_more, base));

    // 4. isLess logic:
    EXPECT_FALSE(Precision::isLess(tiny_bit_less, base));
    EXPECT_TRUE(Precision::isLess(significant_less, base));
}

TEST_F(PrecisionTestSuite, QuantityArithmetic) {
    double qty = 1.0;
    // Subtracting a value smaller than epsilon should result in zero
    // (Or whatever behavior subtract_or_zero implements)
    double very_small = 1e-12; 
    
    double result = 1.0;
    Precision::subtract_or_zero(result, 1.0 + very_small);
    EXPECT_DOUBLE_EQ(result, 0.0);
}