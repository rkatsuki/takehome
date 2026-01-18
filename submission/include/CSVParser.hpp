#pragma once

#include <string>
#include <string_view>
#include "OutputHandler.hpp"
#include "TradingEngine.hpp"

class CSVParser {
public:
    explicit CSVParser(OutputHandler& handler);
    bool parseAndExecute(const std::string& raw, TradingEngine& engine);

protected:
    std::string_view get_token(std::string_view& data);
    bool try_parse_double(std::string_view sv, double& value);
    
    // Two parameters, non-static
    bool try_parse_uint64_t(std::string_view sv, uint64_t& value);

private:
    OutputHandler& outputHandler_; // Shared reference
};