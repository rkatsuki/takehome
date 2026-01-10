# Kraken Interview Test Runner - Sample Environment

Welcome to your Kraken interview adventure! This sample environment lets you dive in and practice with the same Docker-based testing framework you'll use for your actual take-home assignment.

## 🎯 Purpose

This is a **sample environment** to help you:
- Verify your Docker setup works correctly
- Understand the submission format and test process
- Practice the workflow before tackling the actual assessment

The included example solves a simple problem: **reversing strings from stdin**.

## 🚀 Quick Start

Run the tests using Docker (works on all platforms):

```bash
# Build the Docker image
docker build -t kraken-test .

# Run tests (reports saved to ./reports/)
docker run --rm -v "$(pwd)/reports:/reports" kraken-test
```

This will:
1. Run your submission against the test cases
2. Display test results in the terminal
3. Save reports to `./reports/` directory

## 📁 Project Structure

```
.
├── Dockerfile          # Docker environment setup
├── submission/         # Your solution goes here
│   ├── CMakeLists.txt # Build configuration (must produce 'kraken_submission' binary)
│   └── src/
│       └── main.cpp   # Your code
├── test/              # Test framework
│   ├── run_tests.sh   # Test runner script
│   └── 1/, 2/, 3/     # Test cases (DO NOT MODIFY)
└── reports/           # Test reports (generated after running tests)
```

## 📝 Requirements

Your submission must:
- Contain build configuration that produces a binary named `kraken_submission`
- Read input from stdin
- Write output to stdout
- Process data correctly according to test expectations

## ✅ Example Problem

The included example reads lines from stdin and prints them reversed:

**Input:**
```
hello world
test line
```

**Output:**
```
dlrow olleh
enil tset
```

## 🛠️ Docker Commands

```bash
# Build the image
docker build -t kraken-test .

# Run tests (reports saved to ./reports/)
docker run --rm -v "$(pwd)/reports:/reports" kraken-test

# Clean up
docker rmi kraken-test
```

## 📊 Test Reports

After running tests, check the `./reports/` directory:
- `report.xml` - JUnit XML format (for CI/CD integration)
- `report.html` - Human-readable HTML report

Test results are also displayed in the terminal.

## 🧰 Available Libraries

We've pre-installed some common libraries for your convenience - **but they're completely optional!** Use them if you like, or ignore them entirely.

### C++ Environment

The C++ Docker image (Ubuntu 24.04) includes:

**Build Tools:**
- CMake (build system)
- Ninja Build
- GCC and Clang compilers
- C++20 standard support

**Libraries:**
- **Boost** - Comprehensive C++ libraries (including asio for networking)
- **spdlog** - Fast C++ logging library
- **nlohmann-json** - JSON for Modern C++
- **pthreads** - Threading support

**Testing & Benchmarking:**
- **Google Test** - C++ testing framework
- **Google Mock** - C++ mocking framework
- **Catch2** - Modern C++ testing framework
- **Google Benchmark** - Microbenchmark support library

**Development Tools:**
- clang-format (code formatting)
- clang-tidy (static analysis)

**Want something else?** No problem! You have full freedom to bring your own dependencies. Just include them in your `submission/` folder and configure your build system (CMakeLists.txt or Cargo.toml) to fetch and build them. Your environment, your choice.

If you do bring in external libraries, we'd love to hear your reasoning! A quick note in a comment or README explaining why you chose them helps us understand your thought process.

## 📖 Next Steps

Once you're comfortable with this environment:
1. Make sure all tests pass ✅
2. Try modifying the code and see tests fail (it's fun, trust us!)
3. Move on to the actual assessment when you're ready

The assessment environment will work exactly the same way - you'll just be solving a different (and more interesting!) problem.

## ❓ Troubleshooting

**Docker build fails:**
- Make sure Docker is installed and running
- Check you have internet connection (for downloading dependencies)

**Tests don't run:**
- Ensure your binary is named exactly `kraken_submission`
- Check that your program reads from stdin and writes to stdout

**Need help?**
- Read the error messages carefully - they're usually more helpful than they first appear!
- Check the `./reports/` directory for detailed test output
- Contact your recruiter if you have technical issues - we want you to succeed!

---

**Ready to dive in? Let's go!** 🚀

