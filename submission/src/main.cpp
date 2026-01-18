#include "TradingApp.hpp"

int main() {
    try {
        TradingApp app;
        app.run(); 
    } catch (const std::exception& e) {
        return 1;
    }
    return 0;
}