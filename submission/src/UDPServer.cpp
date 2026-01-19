#include "UDPServer.hpp"
#include <iostream>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/**
 * @brief Updated Constructor
 * @details Receives the shared_ptr to the queue directly. This ensures the 
 * queue stays alive even if the TradingApp is destroyed.
 */
UDPServer::UDPServer(std::shared_ptr<ThreadSafeQueue<std::string>> inputQueue) 
    : inputQueue_(std::move(inputQueue)), 
      sockfd_(-1),
      running_(false) {
    
    // 1. Pull values from Config
    int port = Config::Network::UDP_PORT;
    const std::string& ip = Config::Network::SERVER_IP;
    int rcvbuf = Config::Network::SO_RCVBUF_SIZE;

    sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd_ < 0) [[unlikely]] {
        perror("Socket creation failed");
        return;
    }

    // 2. Hardware/Kernel Tuning
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    int opt = 1;
    setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 3. Bind to Address
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr) <= 0) {
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    
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
    
    // Start the receiver thread. We no longer need a workerLoop thread 
    // because we are pushing directly into the App's inputQueue_.
    receiverThread_ = std::thread(&UDPServer::receiverLoop, this);
    
    // std::cout << "SERVER_START: Listening on UDP port " << Config::Network::UDP_PORT << std::endl;
}

void UDPServer::stop() {
    bool wasRunning = running_.exchange(false);
    if (!wasRunning) return;

    if (sockfd_ >= 0) {
        // 1. Use SHUT_RDWR to be absolutely sure the kernel releases the block
        shutdown(sockfd_, SHUT_RDWR); 
    }

    // 2. The thread should now fall out of recvfrom and exit the loop
    if (receiverThread_.joinable()) {
        receiverThread_.join();
    }

    if (sockfd_ >= 0) {
        close(sockfd_);
        sockfd_ = -1;
    }
}

/**
 * @brief Zero-Copy Optimized Receiver
 */
void UDPServer::receiverLoop() {
    char buffer[4096];
    sockaddr_in cliaddr{};
    socklen_t len = sizeof(cliaddr);

    while (running_.load()) [[likely]] {
        ssize_t n = recvfrom(sockfd_, buffer, sizeof(buffer) - 1, 0, 
                             (struct sockaddr *)&cliaddr, &len);
        
        if (n > 0) [[likely]] {
            inputQueue_->push(std::string(buffer, n));
        } else if (n < 0) {
            // If shutdown() was called, n will be -1.
            // Check if we were told to stop. If so, exit the loop NOW.
            if (!running_.load()) break; 
            
            // Otherwise, it was just a transient error or EINTR
            continue;
        }
    }
    std::cerr << "UDP_SERVER: Receiver loop exited." << std::endl;
}