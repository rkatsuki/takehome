#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <netinet/in.h>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

class UDPServer {
public:
    // The callback takes the raw string received from the wire
    using MessageCallback = std::function<void(const std::string&)>;

    UDPServer(int port, MessageCallback callback);
    ~UDPServer();

    void start();
    void stop();

private:
    void receiverLoop();
    void workerLoop();

    int port_;
    int sockfd_;
    MessageCallback onMessage_;
    
    std::atomic<bool> running_{false};
    std::thread receiverThread_;
    std::thread workerThread_;

    // Producer-Consumer Queue to prevent dropping UDP packets
    std::queue<std::string> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
};