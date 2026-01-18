#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "Constants.hpp"
#include "ThreadSafeQueue.hpp"

class UDPServer {
public:
    explicit UDPServer(std::shared_ptr<ThreadSafeQueue<std::string>> inputQueue);
    ~UDPServer();
    void start();
    void stop();

private:
    void receiverLoop();

    std::shared_ptr<ThreadSafeQueue<std::string>> inputQueue_;
    int sockfd_;
    int port_;
    std::atomic<bool> running_;
    std::thread receiverThread_;
};