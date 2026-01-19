#include <gtest/gtest.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <vector>

class KrakenIntegrationTest : public ::testing::Test {
protected:
    pid_t app_pid = -1;
    int pipe_fd[2]; // [0] is read end, [1] is write end
    int udp_sock = -1;

    void SetUp() override {
        if (pipe(pipe_fd) == -1) FAIL() << "Pipe failed";

        app_pid = fork();
        if (app_pid == 0) {
            dup2(pipe_fd[1], STDOUT_FILENO);
            close(pipe_fd[0]);
            close(pipe_fd[1]);

            // Option A: Use 'stdbuf' to force line-buffering at the OS level
            char* args[] = {(char*)"stdbuf", (char*)"-oL", (char*)"./build/kraken_submission", nullptr};
            execvp(args[0], args);
            exit(1);
        } else {
            close(pipe_fd[1]);
            udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
            
            // Let's give it a full second to be absolutely sure
            std::this_thread::sleep_for(std::chrono::seconds(1)); 
        }
    }
    void TearDown() override {
        if (app_pid > 0) {
            kill(app_pid, SIGTERM);
            waitpid(app_pid, nullptr, 0);
        }
        close(pipe_fd[0]);
        close(udp_sock);
    }

    void sendUDP(const std::string& msg) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(1234);
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        sendto(udp_sock, msg.c_str(), msg.length(), 0, (struct sockaddr*)&addr, sizeof(addr));
    }

    // Helper to read a line from the pipe with a timeout to prevent stalls
    std::string readLine(int timeoutMs = 1000) {
        std::string line;
        char c;
        struct poll_fd { int fd; short events; } pfd = {pipe_fd[0], POLLIN};

        while (true) {
            int ret = poll((struct pollfd*)&pfd, 1, timeoutMs);
            if (ret <= 0) break; // Timeout or error

            if (read(pipe_fd[0], &c, 1) > 0) {
                if (c == '\n') break;
                line += c;
            } else {
                break;
            }
        }
        return line;
    }
};

// --- Mimicking the Reviewer's Test Logic ---

TEST_F(KrakenIntegrationTest, Scenario1_FullFlow) {
    // 1. Send test data
    sendUDP("N, 1, IBM, 10, 100, B, 1");
    sendUDP("N, 1, IBM, 12, 100, S, 2");
    sendUDP("N, 2, IBM, 9, 100, B, 101");
    sendUDP("N, 2, IBM, 11, 100, S, 102");
    sendUDP("N, 1, IBM, 11, 100, B, 3");
    sendUDP("N, 2, IBM, 10, 100, S, 103");
    sendUDP("N, 1, IBM, 10, 100, B, 4");
    sendUDP("N, 2, IBM, 11, 100, S, 104");
    sendUDP("F");

    int lines_expected = 17;
    // 2. Collect exactly 5 lines (based on scenario expectations)
    std::vector<std::string> output;
    for (int i = 0; i < lines_expected; ++i) {
        std::string line = readLine();
        std::cout << "DEBUG: Received line: " << line << std::endl; // Add this
        if (!line.empty()) output.push_back(line);
    }
    // 3. Strict Verification
    EXPECT_EQ(output.size(), lines_expected);
    EXPECT_EQ(output[0], "A, 1, 1");
    EXPECT_EQ(output[1], "B, B, 10, 100");
    EXPECT_EQ(output[2], "A, 1, 2");
    EXPECT_EQ(output[3], "B, S, 12, 100");
    EXPECT_EQ(output[4], "A, 2, 101");
    EXPECT_EQ(output[5], "A, 2, 102");
    EXPECT_EQ(output[6], "B, S, 11, 100");
    EXPECT_EQ(output[7], "A, 1, 3");
    EXPECT_EQ(output[8], "T, 1, 3, 2, 102, 11, 100");
    EXPECT_EQ(output[9], "B, S, 12, 100");
    EXPECT_EQ(output[10], "A, 2, 103");
    EXPECT_EQ(output[11], "T, 1, 1, 2, 103, 10, 100");
    EXPECT_EQ(output[12], "B, B, 9, 100");
    EXPECT_EQ(output[13], "A, 1, 4");
    EXPECT_EQ(output[14], "B, B, 10, 100");
    EXPECT_EQ(output[15], "A, 2, 104");
    EXPECT_EQ(output[16], "B, S, 11, 100");
}