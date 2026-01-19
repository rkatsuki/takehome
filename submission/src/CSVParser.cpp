#include "CSVParser.hpp"
#include <charconv>
#include <iostream>
#include <system_error>
#include <format>

CSVParser::CSVParser(OutputHandler& handler) : outputHandler_(handler) {}

/**
 * @brief Non-Destructive Tokenizer
 * @details This function "slices" the input string_view without copying data.
 * It uses pointer arithmetic to move the 'data' window forward.
 */
std::string_view CSVParser::get_token(std::string_view& data) {
    if (data.empty()) return {};

    size_t pos = data.find(',');
    std::string_view token;

    if (pos == std::string_view::npos) {
        token = data;
        data = {}; // End of string reached
    } else {
        token = data.substr(0, pos);
        data.remove_prefix(pos + 1); // Advance beyond the comma
    }

    /**
     * @note ROBUST TRIMMING (In-Place)
     * We adjust the view boundaries to ignore whitespace. This is O(1) 
     * and avoids creating new string objects.
     */
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
        token.remove_prefix(1);
    }
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
        token.remove_suffix(1);
    }

    return token;
}

/**
 * @brief The Parser Hot-Path
 * @details This function transforms raw UDP text into a binary 'Command' 
 * object. It follows a "Validate-Everything-Early" philosophy to ensure 
 * the TradingEngine never receives corrupted data.
 */
bool CSVParser::parseAndExecute(const std::string& raw, TradingEngine& engine) {
    std::string_view data(raw);
    
    // 1. Initial Boundary Trimming
    size_t first = data.find_first_not_of(" \n\r\t");
    if (first == std::string_view::npos) return false; 
    size_t last = data.find_last_not_of(" \n\r\t");
    data = data.substr(first, (last - first + 1));

    // 2. Identify Command Type (Dispatch Byte)
    std::string_view type_sv = get_token(data);
    if (type_sv.empty()) [[unlikely]] return false;
    char type = type_sv[0];

    Command cmd;
    
    /**
     * @note Command-Specific Parsing Logic
     * We use if/else with branch hints. In a typical feed, 'NEW' and 'CANCEL' 
     * are the dominant message types.
     */
    if (type == 'N') [[likely]] {
        // Sequence: N, userId, symbol, price, quantity, side, userOrderId
        std::string_view uid_sv  = get_token(data); // 1. userId
        std::string_view sym_sv  = get_token(data); // 2. symbol
        std::string_view prc_sv  = get_token(data); // 3. price
        std::string_view qty_sv = get_token(data); // 4. quantity
        std::string_view side_sv  = get_token(data); // 5. side
        std::string_view oid_sv  = get_token(data); // 6. userOrderId

        if (oid_sv.empty()) [[unlikely]] {
            outputHandler_.printReject(0, 0, "Incommplete or Malformed Request");
            return false;
        }

        // Binary Conversion for IDs
        if (!try_parse_uint64_t(uid_sv, cmd.userId) || !try_parse_uint64_t(oid_sv, cmd.userOrderId)) [[unlikely]] {
            // Ids are invalid, so we don't know who to send the error to!
            outputHandler_.printReject(0, 0, "Invalid ID");
            return false;
        }

        // 1. Handle Price and OrderType first
        if (!try_parse_double(prc_sv, cmd.price)) {
            outputHandler_.printReject(cmd.userId, cmd.userOrderId, "Invalid Price Value");
            return false;
        }

        // Check if the price is exactly 0, if so Market Order
        if (cmd.price == 0.0) {
            cmd.orderType = OrderType::MARKET;
        } else {
            cmd.orderType = OrderType::LIMIT;
        }

        // 2. Quantity MUST still be positive (even for Market orders)
        if (!try_parse_double(qty_sv, cmd.quantity)) [[unlikely]] {
            outputHandler_.printReject(cmd.userId, cmd.userOrderId, "Invalid Quantity: Malformed");
            return false;
        }

        /**
         * @note Symbol Validation (12+1 Length)
         * We verify against Config::SYMBOL_LENGTH to prevent buffer overflows 
         * when initializing the fixed-width Symbol struct.
         */
        if (sym_sv.empty() || sym_sv.length() > Config::SYMBOL_LENGTH) [[unlikely]] {
            outputHandler_.printReject(cmd.userId, cmd.userOrderId, "Invalid Symbol: Buffer Limit");
            return false;
        }
        cmd.symbol = Symbol(sym_sv);
        cmd.type = CommandType::NEW;
        
        // Side Mapping
        if (side_sv == "B") cmd.side = Side::BUY;
        else if (side_sv == "S") cmd.side = Side::SELL;
        else [[unlikely]] {
            outputHandler_.printReject(cmd.userId, cmd.userOrderId, "Invalid Side");
            return false;
        }
    } 
    else if (type == 'C') {
        // Expected Format: C, userId, userOrderId
        std::string_view uid_sv = get_token(data);
        std::string_view oid_sv = get_token(data);

        if (oid_sv.empty()) [[unlikely]] {
            outputHandler_.logError(std::format("Parse Error: Truncated CANCEL: {}", raw));
            return false;
        }

        cmd.type = CommandType::CANCEL;
        if (!try_parse_uint64_t(uid_sv, cmd.userId) || !try_parse_uint64_t(oid_sv, cmd.userOrderId)) [[unlikely]] {
            outputHandler_.logError(std::format("Parse Error: Invalid ID in CANCEL: {}", raw));
            return false;
        }
    } 
    else if (type == 'F') {
        cmd.type = CommandType::FLUSH;
    } 
    else [[unlikely]] {
        outputHandler_.logError(std::format("Parse Error: Unknown Type '{}'", type));
        return false;
    }

    /**
     * @note Garbage Detection
     * A strict parser ensures no extra data exists after the expected fields.
     * This catches malformed CSVs that might otherwise lead to logical errors.
     */
    if (!data.empty()) [[unlikely]] {
        outputHandler_.logError(std::format("Parse Error: Extra fields in: {}", raw));
        return false;
    }

    // Hand off the validated binary command to the engine
    engine.processCommand(cmd);
    return true;
}





bool CSVParser::try_parse_uint64_t(std::string_view sv, uint64_t& value) {
    if (sv.empty()) [[unlikely]] return false;

    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    
    // ec == std::errc{} ensures no overflow (result_out_of_range)
    // ptr == end ensures the entire string was a valid number
    return (ec == std::errc{}) && (ptr == sv.data() + sv.size());
}

bool CSVParser::try_parse_double(std::string_view sv, double& value) {
    if (sv.empty()) return false;
    
    // std::from_chars is excellent, but it returns errc::invalid_argument for "NaN"
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    
    if (ec != std::errc{} || ptr != sv.data() + sv.size() || !std::isfinite(value)) {
        value = -1.0; // Set poison value
        return false; // Still return false to let the caller know it's "bad"
    }
    return true;
}
