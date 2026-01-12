#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

#include "TradingEngine.hpp"

using namespace std;

// Formatting helper to ensure we match exchange precision requirements
string formatPrice(double price) {
    stringstream ss;
    ss << fixed << setprecision(2) << price;
    return ss.str();
}

/**
 * Presenter: Formats EngineResponse for stdout.
 * Separates the "What happened" from the "How to show it".
 */
void presentResponse(const EngineResponse& resp) {
    if (!resp.isSuccess()) {
        cerr << "Error [" << resp.statusCode << "]: " << resp.message << endl;
        return;
    }

    if (auto* ack = std::get_if<OrderAcknowledgement>(&resp.data)) {
        cout << "ACCEPTED," << ack->tag << "," << ack->orderID << endl;
    }
    else if (auto* execs = std::get_if<std::vector<Execution>>(&resp.data)) {
        for (const auto& e : *execs) {
            cout << "TRADE," << e.symbol << "," 
                 << formatPrice(e.price) << "," << e.quantity << endl;
        }
    } 
    else if (auto* snap = std::get_if<OrderBookSnapshot>(&resp.data)) {
        cout << "BOOK_START," << snap->symbol << endl;
        for (const auto& b : snap->bids) cout << "BID," << formatPrice(b.price) << "," << b.size << endl;
        for (const auto& a : snap->asks) cout << "ASK," << formatPrice(a.price) << "," << a.size << endl;
        cout << "BOOK_END" << endl;
    }
}

/**
 * Unified Parser: Handles strings regardless of source (UDP/stdin)
 */
void processLine(const string& line, TradingEngine& engine) {
    if (line.empty() || line[0] == '#') return;

    stringstream ss(line);
    string cmd;
    if (!getline(ss, cmd, ',')) return;
    
    if (cmd == "ORDER") {
        string tag, symbol, sideStr, typeStr, qtyStr, priceStr;
        getline(ss, tag, ','); getline(ss, symbol, ',');
        getline(ss, sideStr, ','); getline(ss, typeStr, ',');
        getline(ss, qtyStr, ','); getline(ss, priceStr, ',');
        Order ord{
            .tag = tag,
            .symbol = symbol,
            .side = (sideStr == "BUY" || sideStr == "B") ? Side::BUY : Side::SELL,
            .type = (typeStr == "MARKET" || typeStr == "M") ? OrderType::MARKET : OrderType::LIMIT,
            .price = priceStr.empty() ? 0.0 : stod(priceStr),
            .quantity = stol(qtyStr)
        };
        presentResponse(engine.submitOrder(ord));
    } 
    else if (cmd == "CANCEL_TAG") {
        string tag, symbol;
        getline(ss, tag, ',');
        getline(ss, symbol, ','); // Now expecting symbol for O(1) routing        
        presentResponse(engine.cancelOrderByTag(tag, symbol));
    } 
    else if (cmd == "CANCEL_ID") {
        string idStr;
        getline(ss, idStr, ',');
        long id = stol(idStr);
        presentResponse(engine.cancelOrderById(id));
    } else if (cmd == "ORDERBOOK") {
        string symbol, depthStr;
        getline(ss, symbol, ',');
        int depth = getline(ss, depthStr, ',') ? stoi(depthStr) : 1;
        presentResponse(engine.getOrderBook(symbol, depth));
    } 
    else if (cmd == "FLUSH") {
        presentResponse(engine.reportExecutions());
    }
}

int main(int argc, char* argv[]) {
    TradingEngine engine;
    
    // Check for --mode udp
    bool udpMode = false;
    for (int i = 1; i < argc; ++i) {
        if (string(argv[i]) == "--mode" && i + 1 < argc) {
            if (string(argv[i+1]) == "udp") udpMode = true;
        }
    }

    if (udpMode) {
        int sockfd;
        struct sockaddr_in servaddr, cliaddr;
        
        if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("Socket creation failed");
            return -1;
        }

        memset(&servaddr, 0, sizeof(servaddr));
        servaddr.sin_family = AF_INET;
        servaddr.sin_addr.s_addr = INADDR_ANY;
        servaddr.sin_port = htons(12345);

        if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
            perror("Bind failed");
            close(sockfd);
            return -1;
        }

        cerr << "Engine live! Listening for UDP on port 12345..." << endl;

        char buffer[2048];
        while (true) {
            socklen_t len = sizeof(cliaddr);
            int n = recvfrom(sockfd, buffer, sizeof(buffer)-1, 0, (struct sockaddr *)&cliaddr, &len);
            if (n > 0) {
                buffer[n] = '\0';
                processLine(string(buffer), engine);
            }
        }
        close(sockfd);
    } 
    else {
        // Fallback to standard input (cin)
        string line;
        while (getline(cin, line)) {
            processLine(line, engine);
        }
    }

    return 0;
}
