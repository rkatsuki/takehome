#include <gtest/gtest.h>
#include <fstream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <thread>
#include <sys/wait.h>

// 1. Define the Parameter Structure
struct FilePair {
    std::string inputPath;
    std::string outputPath;
    std::string testName;
};

// 2. The Base Fixture containing the Fork/Socket Logic
class KrakenFileParamTest : public ::testing::TestWithParam<FilePair> {
protected:
    pid_t app_pid = -1;
    int pipe_fd[2]; 
    int udp_sock = -1;

    void SetUp() override {
        if (pipe(pipe_fd) == -1) FAIL() << "Pipe failed";

        app_pid = fork();
        if (app_pid == 0) {
            // CHILD PROCESS: Redirect stdout to pipe and run app
            dup2(pipe_fd[1], STDOUT_FILENO);
            close(pipe_fd[0]);
            close(pipe_fd[1]);

            // Using stdbuf to prevent the OS from buffering output
            char* args[] = {(char*)"stdbuf", (char*)"-oL", (char*)"./build/kraken_submission", nullptr};
            execvp(args[0], args);
            exit(1);
        } else {
            // PARENT PROCESS
            close(pipe_fd[1]);
            udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
            
            // Give the app a moment to start up
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
            
            // DRAIN: Clear out any startup noise (like "Global state reset")
            while (!readLine(10).empty()); 
        }
    }

    void TearDown() override {
        if (app_pid > 0) {
            kill(app_pid, SIGTERM);
            waitpid(app_pid, nullptr, 0);
        }
        close(pipe_fd[0]);
        if (udp_sock != -1) close(udp_sock);
    }

    void sendUDP(const std::string& msg) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(1234); 
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        sendto(udp_sock, msg.c_str(), msg.length(), 0, (struct sockaddr*)&addr, sizeof(addr));
    }

    std::string readLine(int timeoutMs = 1000) {
        std::string line;
        char c;
        struct pollfd pfd = {pipe_fd[0], POLLIN, 0};

        while (true) {
            int ret = poll(&pfd, 1, timeoutMs);
            if (ret <= 0) break; 

            if (read(pipe_fd[0], &c, 1) > 0) {
                if (c == '\n') break;
                line += c;
            } else break;
        }
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line;
    }
};

// 3. The Actual Test Logic
TEST_P(KrakenFileParamTest, ExecuteScenario) {
    const FilePair& params = GetParam();
    
    // Load Expected Results
    std::ifstream outFile(params.outputPath);
    ASSERT_TRUE(outFile.is_open()) << "Failed to open output: " << params.outputPath;
    
    std::vector<std::string> expected;
    std::string line;
    while (std::getline(outFile, line)) {
        if (!line.empty()) expected.push_back(line);
    }

    // Stream Input CSV via UDP
    std::ifstream inFile(params.inputPath);
    ASSERT_TRUE(inFile.is_open()) << "Failed to open input: " << params.inputPath;
    
    while (std::getline(inFile, line)) {
        if (!line.empty()) {
            sendUDP(line);
            // Allow small gap for high-frequency processing stability
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    // Compare actual output from pipe to expected file content
    for (size_t i = 0; i < expected.size(); ++i) {
        std::string actual = readLine(2000); 

        std::cout << actual << std::endl;

        EXPECT_EQ(actual, expected[i]) 
            << "Mismatch in [" << params.testName << "] at line " << i + 1;
    }
}

// // 4. Register your pairs here
// INSTANTIATE_TEST_SUITE_P(
//     CSVScenarios,
//     KrakenFileParamTest,
//     ::testing::Values(
//         FilePair{"/test/1/in.csv", "/test/1/out.csv", "Scenario_1"},
//         FilePair{"/test/3/in.csv", "/test/3/out.csv", "Scenario_3"},
//         FilePair{"/test/5/in.csv", "/test/5/out.csv", "Scenario_5"},
//         FilePair{"/test/7/in.csv", "/test/7/out.csv", "Scenario_7"},
//         FilePair{"/test/9/in.csv", "/test/9/out.csv", "Scenario_9"},
//         FilePair{"/test/11/in.csv", "/test/11/out.csv", "Scenario_11"},
//         FilePair{"/test/13/in.csv", "/test/13/out.csv", "Scenario_13"},
//         FilePair{"/test/15/in.csv", "/test/15/out.csv", "Scenario_15"}
//     ),
//     [](const ::testing::TestParamInfo<FilePair>& info) {
//         return info.param.testName;
//     }
// );

// 4. Helper function to generate all 16 test cases
std::vector<FilePair> GenerateAllScenarios() {
    std::vector<FilePair> scenarios;
    for (int i = 1; i <= 16; ++i) {
        // Use "expected.csv" for all, assuming you named your custom outputs 
        // to match the challenge's naming convention for ground truth.
        // std::string folder = "/test/" + std::to_string(i);   // project root foler
        std::string folder = "./tests/data/" + std::to_string(i);   // submission folder
        scenarios.push_back({
            folder + "/in.csv", 
            folder + "/out.csv", // Adjusted to 'expected.csv' based on typical Kraken naming
            "Scenario_" + std::to_string(i)
        });
    }
    return scenarios;
}

// 5. Register the suite using the generator function
INSTANTIATE_TEST_SUITE_P(
    CSVScenarios,
    KrakenFileParamTest,
    ::testing::ValuesIn(GenerateAllScenarios()),
    [](const ::testing::TestParamInfo<FilePair>& info) {
        return info.param.testName;
    }
);