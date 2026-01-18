#include "UDPServer.hpp"

#include <iostream>
#include <unistd.h>
#include <cstring>
#include <format>

#include <sys/socket.h>
#include <netinet/in.h>

/**
 * @brief Network Entry Point
 * @details We initialize the socket using SOCK_DGRAM (UDP). Unlike TCP, UDP 
 * is connectionless, making it significantly faster for market data feeds 
 * but requiring us to handle message boundaries and potential loss ourselves.
 */
UDPServer::UDPServer(int port, MessageCallback callback) 
    : port_(port), onMessage_(std::move(callback)), sockfd_(-1) {
    
    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) [[unlikely]] {
        perror("Socket creation failed");
        return;
    }

    /**
     * @note Socket Options:
     * A senior engineer would often set SO_RCVBUF here to increase the kernel's
     * UDP buffer size, preventing drops during sudden market "bursts."
     */
    int opt = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(port_);

    if (bind(sockfd_, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) [[unlikely]] {
        perror("Bind failed");
        close(sockfd_);
        sockfd_ = -1;
    }
}

UDPServer::~UDPServer() {
    stop();
}

void UDPServer::start() {
    if (sockfd_ < 0) return;
    running_ = true;
    
    // Split concerns: One thread to capture packets, one to process them.
    workerThread_ = std::thread(&UDPServer::workerLoop, this);
    receiverThread_ = std::thread(&UDPServer::receiverLoop, this);
    
    std::cout << "SERVER_START: Listening on UDP port " << port_ << std::endl;
}

/**
 * @brief Graceful Shutdown Logic
 * @details We use shutdown(SHUT_RD) to unblock the recvfrom() system call. 
 * This ensures the thread can check the 'running_' flag and exit cleanly 
 * rather than hanging forever on an idle socket.
 */
void UDPServer::stop() {
    running_ = false;
    cv_.notify_all(); 
    if (sockfd_ >= 0) {
        // Force the blocking recvfrom to return 0 or error
        shutdown(sockfd_, SHUT_RD); 
        close(sockfd_);
        sockfd_ = -1;
    }
    if (receiverThread_.joinable()) receiverThread_.join();
    if (workerThread_.joinable()) workerThread_.join();
}

/**
 * @brief The "Fast Path" Receiver
 * @details This thread has ONE job: copy data from the kernel to the app 
 * as fast as possible. We minimize logic here to prevent buffer overflows.
 */
void UDPServer::receiverLoop() {
    // 4KB is standard for UDP MTU safety (most packets are < 1500 bytes)
    char buffer[4096];
    sockaddr_in cliaddr{};
    socklen_t len = sizeof(cliaddr);

    while (running_) [[likely]] {
        /**
         * @note RECVFROM (Blocking):
         * This call yields the CPU until a packet arrives. When it returns, 
         * we immediately move the data to a thread-safe queue.
         */
        ssize_t n = recvfrom(sockfd_, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cliaddr, &len);
        
        if (n > 0) [[likely]] {
            buffer[n] = '\0';
            
            /**
             * @note Critical Section:
             * We wrap the packet in a std::string and push it. 
             * In the next optimization phase, we would replace this with a 
             * RingBuffer of pre-allocated char arrays to avoid the 'new' call 
             * inside std::string.
             */
            {
                std::lock_guard<std::mutex> lock(mtx_);
                queue_.push(std::string(buffer));
            }
            cv_.notify_one();
        } else if (n < 0 && running_) {
            // Log networking errors to the diagnostic channel
            // In a production environment, we might track 'packets dropped' stats here.
        }
    }
}

/**
 * @brief The Deserialization Worker
 * @details This thread bridges the raw network bytes and the Trading Engine. 
 * By separating this, a heavy parsing job won't stop the 'receiverLoop' 
 * from picking up the next UDP packet.
 */
void UDPServer::workerLoop() {
    while (running_) [[likely]] {
        std::string msg;
        {
            /**
             * @note Thread Orchestration:
             * unique_lock + condition_variable ensures this thread consumes 
             * 0% CPU while the market is quiet.
             */
            std::unique_lock<std::mutex> lock(mtx_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });
            
            if (!running_ && queue_.empty()) break;
            
            // Move semantics ensure we aren't copying the packet string again.
            msg = std::move(queue_.front());
            queue_.pop();
        }
        
        // Pass to the CSVParser/TradingEngine callback
        if (!msg.empty() && onMessage_) [[likely]] {
            onMessage_(msg);
        }
    }
}