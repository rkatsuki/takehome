#include "main.hpp"
#include <thread>
#include <chrono>

// Atomic flag to signal the listener thread to stop on exit
std::atomic<bool> keepRunning{true};

void resultListener(ThreadSafeQueue<EngineResponse>& responseQueue) {
    while (keepRunning) {
        // 1. Block here until at least one thing exists
        EngineResponse firstResp = responseQueue.wait_and_pop();
        
        // Check for "Poison Pill" (sentinel for shutdown)
        if (!keepRunning && firstResp.message == "SHUTDOWN_SENTINEL") break;

        handleResponse(firstResp);

        // 2. Drain any "burst" items that arrived at the same time
        while (auto nextResp = responseQueue.try_pop()) {
            handleResponse(*nextResp);
            // Optional: add a tiny sleep here if you want to see them scroll
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 3. Only now that the queue is empty, we return the prompt
        std::cout << "engine> " << std::flush;
    }
}

void displayOrderReport(const Order& o) {
    auto sideStr = (o.side == Side::BUY) ? "BUY" : "SELL";
    auto statusStr = "UNKNOWN";
    if (o.status == OrderStatus::ACTIVE) statusStr = "ACTIVE";
    else if (o.status == OrderStatus::FILLED) statusStr = "FILLED";
    else if (o.status == OrderStatus::CANCELLED) statusStr = "CANCELLED";

    std::cout << std::format(
        "  [ORDER REPORT]\n"
        "  ID:      {}\n"
        "  Sym:     {}\n"
        "  Side:    {}\n"
        "  Price:   {:.2f}\n"
        "  RemQty:  {:.4f}\n"
        "  Status:  {}\n",
        o.orderID, o.symbol.c_str(), sideStr, o.price, 
        o.remainingQuantity, statusStr
    );
}

void displayBook(const OrderBookSnapshot& snap) {
    std::cout << "\n--- MARKET: " << snap.symbol.c_str() << " (Seq: " << snap.updateSeq << ") ---\n";
    std::cout << std::setw(10) << "Price" << " | " << std::setw(10) << "Volume" << "\n";
    std::cout << "---------------------------\n";

    for (auto it = snap.asks.rbegin(); it != snap.asks.rend(); ++it) {
        std::cout << "\033[1;31m" << std::setw(10) << it->price << "\033[0m | " 
                  << std::setw(10) << it->quantity << "\n";
    }
    std::cout << "  ---------- SPREAD ----------\n";
    for (const auto& level : snap.bids) {
        std::cout << "\033[1;32m" << std::setw(10) << level.price << "\033[0m | " 
                  << std::setw(10) << level.quantity << "\n";
    }
    std::cout << "---------------------------\n" << std::endl;
}

void handleResponse(const EngineResponse& resp) {
    if (resp.isSuccess()) {
        std::cout << ">>> SUCCESS: " << resp.message << std::endl;
        if (resp.order) displayOrderReport(*resp.order);
        if (resp.snapshot.has_value()) displayBook(resp.snapshot.value());
    } else {
        std::cout << ">>> ERROR [" << (int)resp.code << "]: " << resp.message << std::endl;
    }
}

int main() {
    TradingEngine engine;
    ThreadSafeQueue<EngineResponse> responseQueue;
    
    // Launch background UI thread
    std::thread listener(resultListener, std::ref(responseQueue));

    std::string line;
    std::cout << "Kraken Performance Engine [Threaded Shell Ready]\n";
    std::cout << "Commands: LIMIT, MARKET, CANCEL, BOOK, QUIT\n" << std::endl;

    while (std::cout << "engine> " && std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::string_view sv(line);
        std::string_view cmd = get_next_token(sv);

        if (cmd.empty() || cmd[0] == '#') continue;

        if (cmd == "QUIT") {
            keepRunning = false;
            
            EngineResponse sentinel;
            sentinel.message = "SHUTDOWN_SENTINEL"; // Unique string to identify exit
            responseQueue.push(sentinel); 
            
            break;
        }
        else if (cmd == "ECHO") {
            EngineResponse resp;
            resp.code = EngineStatusCode::OK;
            resp.message = std::string(sv);
            responseQueue.push(resp);
        }
        else if (cmd == "LIMIT") {
            std::string_view s_side = get_next_token(sv);
            std::string_view sym_name = get_next_token(sv);
            double qty = to_double(get_next_token(sv));
            double price = to_double(get_next_token(sv));
            std::string_view tag = get_next_token(sv);

            Side side = (s_side == "BUY") ? Side::BUY : Side::SELL;
            responseQueue.push(engine.submitOrder(LimitOrderRequest{
                price, qty, side, Symbol{sym_name}, std::string(tag)
            }));
        } 
        else if (cmd == "MARKET") {
            std::string_view s_side = get_next_token(sv);
            std::string_view sym_name = get_next_token(sv);
            double qty = to_double(get_next_token(sv));
            std::string_view tag = get_next_token(sv);

            Side side = (s_side == "BUY") ? Side::BUY : Side::SELL;
            responseQueue.push(engine.submitOrder(MarketOrderRequest{
                qty, side, Symbol{sym_name}, std::string(tag)
            }));
        } 
        else if (cmd == "CANCEL") {
            OrderID id = to_num<OrderID>(get_next_token(sv));
            responseQueue.push(engine.cancelOrder(id));
        } 
        else if (cmd == "BOOK") {
            std::string_view sym_name = get_next_token(sv);
            int depth = to_num<int>(get_next_token(sv));
            if (depth == 0) depth = 5;
            responseQueue.push(engine.getOrderBookSnapshot(Symbol{sym_name}, depth));
        }
    }

    while (!responseQueue.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    keepRunning = false;
    if (listener.joinable()) listener.join();

    std::cout << "\n[System] Shutdown complete." << std::endl;
    return 0;
}