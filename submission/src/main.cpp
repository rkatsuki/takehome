#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "TradingEngine.hpp"

using namespace std;

/**
 * @brief Entry point for the Trading System.
 * Parses CSV-formatted commands from standard input and routes them to the TradingEngine.
 */
int main(int argc, char* argv[]) {
    // Core Orchestrator
    TradingEngine engine;

    string line;
    // Pre-allocate strings for parsing to avoid repeated heap allocations
    string operation, tag, symbol, side_str, type_str, price_str, qty_str;

    while (getline(cin, line)) {
        // Skip empty lines or comments
        if (line.empty() || line[0] == '#') continue;

        stringstream ss(line);
        if (!getline(ss, operation, ',')) continue;

        if (operation == "ORDER") {
            // Mapping the CSV input to the Order struct
            // Input Format: POST,user_tag,symbol,side,type,price,quantity
            getline(ss, tag, ',');
            getline(ss, symbol, ',');
            getline(ss, side_str, ',');
            getline(ss, type_str, ',');
            getline(ss, qty_str, ',');
            if (type_str == "MARKET") {
                // For MARKET orders, price is not applicable
                price_str = "0";
            } else {
                getline(ss, price_str, ',');
            }

            Order order {
                .orderID = 0, // Assigned by the engine
                .tag = tag,   // The user's ID becomes the 'tag'
                .symbol = symbol,
                .side = (side_str == "BUY") ? Side::BUY : Side::SELL,
                .type = (type_str == "LIMIT") ? OrderType::LIMIT : OrderType::MARKET,
                .price = stod(price_str),
                .quantity = stol(qty_str)
            };

            // Execute returns the internal Engine ID if needed for logging
            engine.executeOrder(order);

        } else if (operation == "CANCEL_BY_ID") {
            // Cancel using the engine-generated numeric ID
            string engine_id_str;
            getline(ss, engine_id_str, ',');
            if (!engine_id_str.empty()) {
                engine.executeCancelById(stol(engine_id_str));
            }

        } else if (operation == "CANCEL_BY_TAG") {
            // Support 'DELETE' keyword for backward compatibility with your tag logic
            getline(ss, tag, ',');
            if (!tag.empty()) {
                engine.executeCancelByTag(tag);
            }

        } else if (operation == "ORDERBOOK") {
            // Format: ORDERBOOK,symbol
            getline(ss, symbol, ',');
            engine.printOrderBookState(symbol);

        } else if (operation == "EXECUTION") {
            // Flushes and prints all trade fills across all symbols
            engine.reportExecutions();
        } else {
            cout << "Unknown operation: " << operation << endl;
        }
    }
    return 0;
}