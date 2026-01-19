#include <gtest/gtest.h>
#include "CSVParser.hpp"

/**
 * @brief Accessor Wrapper
 * Inherits from CSVParser to expose protected/private helpers for unit testing.
 */
class CSVParserTester : public CSVParser {
public:
    using CSVParser::CSVParser; 
    using CSVParser::get_token;
    using CSVParser::try_parse_double;
    using CSVParser::try_parse_uint64_t;
};

class CSVParserTestSuite : public ::testing::Test {
protected:
    std::shared_ptr<ThreadSafeQueue<OutputEnvelope>> queue;
    OutputHandler handler;
    TradingEngine engine;
    CSVParserTester parser;

    CSVParserTestSuite() 
        : queue(std::make_shared<ThreadSafeQueue<OutputEnvelope>>()),
          handler(queue),
          engine(handler),
          parser(handler) {}
};

// --- SECTION 1: Tokenizer Helpers ---

TEST_F(CSVParserTestSuite, TokenizerSlicing) {
    std::string_view data = "NEW,123,ETHUSD";
    EXPECT_EQ(parser.get_token(data), "NEW");
    EXPECT_EQ(parser.get_token(data), "123");
    EXPECT_EQ(parser.get_token(data), "ETHUSD");
    EXPECT_TRUE(data.empty());
}

TEST_F(CSVParserTestSuite, TokenizerTrimming) {
    std::string_view data = "  BUY  , 100.50 ,  42 ";
    EXPECT_EQ(parser.get_token(data), "BUY");
    EXPECT_EQ(parser.get_token(data), "100.50");
    EXPECT_EQ(parser.get_token(data), "42");
}

// --- SECTION 2: Numeric Parsing (std::from_chars) ---

TEST_F(CSVParserTestSuite, ParseIntSuccess) {
    uint64_t val = 0;
    EXPECT_TRUE(parser.try_parse_uint64_t("101", val));
    EXPECT_EQ(val, 101);
}

TEST_F(CSVParserTestSuite, ParseIntFailure) {
    uint64_t val = 0;
    EXPECT_FALSE(parser.try_parse_uint64_t("101abc", val));
    EXPECT_FALSE(parser.try_parse_uint64_t("", val));
}

TEST_F(CSVParserTestSuite, ParseDoubleSuccess) {
    double val = 0.0;
    EXPECT_TRUE(parser.try_parse_double("50000.75", val));
    EXPECT_DOUBLE_EQ(val, 50000.75);
}

TEST_F(CSVParserTestSuite, ParseDoubleInvalidChars) {
    double val = 0.0;
    EXPECT_FALSE(parser.try_parse_double("50.0.5", val)); 
    EXPECT_FALSE(parser.try_parse_double("50.0f", val));  
}

// --- SECTION 3: Integration (The Gatekeeper) ---

TEST_F(CSVParserTestSuite, FullPath_RejectExtraFields) {
    std::string badData = "C, 1, 101, extra_column";
    EXPECT_FALSE(parser.parseAndExecute(badData, engine));
}

TEST_F(CSVParserTestSuite, FullPath_RejectTruncatedNew) {
    std::string truncated = "N,BTCUSD,1,1001,B,10.5"; 
    EXPECT_FALSE(parser.parseAndExecute(truncated, engine));
}

TEST_F(CSVParserTestSuite, FullPath_RejectInvalidSide) {
    std::string badSide = "N,BTCUSD,1,1001,X,10.5,50000.0";
    EXPECT_FALSE(parser.parseAndExecute(badSide, engine));
}

TEST_F(CSVParserTestSuite, FullPath_RejectMalformedSymbol) {
    std::string longSym = "N,VERYLONGTICKERNAME-EXCEEDING-LIMIT,1,1001,B,1.0,500.0";
    EXPECT_FALSE(parser.parseAndExecute(longSym, engine));
    
    std::string emptySym = "N,,1,1001,B,1.0,500.0";
    EXPECT_FALSE(parser.parseAndExecute(emptySym, engine));
}

TEST_F(CSVParserTestSuite, FullPath_AcceptValidFlush) {
    EXPECT_TRUE(parser.parseAndExecute("F", engine));
}

TEST_F(CSVParserTestSuite, FullPath_WhitespaceResilience) {
    std::string messy = " N,    1,    IBM,    10,    100, B, 1";
    EXPECT_TRUE(parser.parseAndExecute(messy, engine));
}

// --- SECTION 4: Data Type & Physical Limits ---

TEST_F(CSVParserTestSuite, RejectUint64Overflow) {
    // 2^64 is 18,446,744,073,709,551,616 (Exceeds uint64_t capacity)
    std::string overflowID = "N,BTC,1,18446744073709551616,B,1.0,500.0";
    EXPECT_FALSE(parser.parseAndExecute(overflowID, engine));
}

TEST_F(CSVParserTestSuite, RejectNegativeIDIntoUnsigned) {
    // Standard from_chars for unsigned rejects '-'
    std::string negID = "N,BTC,1,-500,B,1.0,500.0";
    EXPECT_FALSE(parser.parseAndExecute(negID, engine));
}

TEST_F(CSVParserTestSuite, RejectDoubleOverflow) {
    // 2e400 is well beyond the ~1.8e308 limit of IEEE 754 doubles
    std::string hugePrice = "N,BTC,1,101,B,1.0,2e400";
    EXPECT_FALSE(parser.parseAndExecute(hugePrice, engine));
}

// --- SECTION 4: Data Type & Physical Limits ---

TEST_F(CSVParserTestSuite, RejectNegativeIntAsUnsigned) {
    /**
     * @note UNSIGNED ENFORCEMENT
     * Even though -1 is a small number, std::from_chars for uint64_t
     * will see the '-' and stop, failing our ptr == end check.
     */
    uint64_t val = 0;
    EXPECT_FALSE(parser.try_parse_uint64_t("-1", val));
    EXPECT_FALSE(parser.try_parse_uint64_t("-500", val));
}

TEST_F(CSVParserTestSuite, FullPath_RejectNegativeIDsInCSV) {
    /**
     * @note INTEGRATION CHECK
     * Ensures the parseAndExecute loop correctly handles a 
     * negative ID in a NEW order string.
     */
    std::string negUserId = "N,BTCUSD,-1,1001,B,1.0,50000.0";
    std::string negOrderId = "N,BTCUSD,1,-1001,B,1.0,50000.0";

    EXPECT_FALSE(parser.parseAndExecute(negUserId, engine)) << "Should reject negative UserID";
    EXPECT_FALSE(parser.parseAndExecute(negOrderId, engine)) << "Should reject negative OrderID";
}

// --- SECTION 5: Precision, Complexity, and NaN Safety ---

TEST_F(CSVParserTestSuite, RejectExtremePrecisionUnderflow) {
    // Subnormal values
    std::string tinyPrice = "N,BTC,1,101,B,1.0,1e-310";
    EXPECT_FALSE(parser.parseAndExecute(tinyPrice, engine));
}

TEST_F(CSVParserTestSuite, RejectDoubleInfinityAndNaN) {
    // Guard against values that break OrderBook math
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,inf", engine));
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,nan", engine));
}

TEST_F(CSVParserTestSuite, RejectMassiveNumericString) {
    //Complexity attack guard
    std::string massiveZeros = "0." + std::string(5000, '0') + "1";
    std::string raw = "N,BTC,1,101,B,1.0," + massiveZeros;
    EXPECT_FALSE(parser.parseAndExecute(raw, engine));
}

TEST_F(CSVParserTestSuite, RejectDoubleSpecialValues) {
    /**
     * @note INF/NAN GUARD
     * We explicitly test all mathematical non-finite states.
     */
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,inf", engine));
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,-inf", engine));
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,nan", engine));
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,NAN", engine)); // Case sensitivity check
}

TEST_F(CSVParserTestSuite, RejectNegativeDouble) {
    /**
     * @note LOGICAL SIGN GUARD
     * Price and Quantity must be strictly positive (> 0.0).
     */
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,-50.25", engine)); // Negative Price
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,-10.0,500.0", engine)); // Negative Quantity
}

TEST_F(CSVParserTestSuite, RejectZeroValues) {
    /**
     * @note ZERO GUARD
     * An order for 0 quantity or 0 price is functionally invalid.
     */
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,0.0,500.0", engine));
    EXPECT_FALSE(parser.parseAndExecute("N,BTC,1,101,B,1.0,0.0", engine));
}