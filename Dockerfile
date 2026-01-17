FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    bc \
    clang \
    clang-format \
    clang-tidy \
    netcat-openbsd \
    libboost-all-dev \
    libspdlog-dev \
    nlohmann-json3-dev \
    libgtest-dev \
    libgmock-dev \
    catch2 \
    libbenchmark-dev \
    python3 \
    python3-pip \
    && pip3 install --break-system-packages --no-cache-dir junit2html \
    && rm -rf /var/lib/apt/lists/*

COPY ./submission /kraken_submission/
COPY ./test /test

WORKDIR /kraken_submission
RUN mkdir build && cmake -B build && cmake --build build/

# Default test mode (can be overridden with docker run -e TEST_MODE=udp)
ENV TEST_MODE=udp

ENTRYPOINT ["/bin/bash", "-c", "cd /test && ./run_tests.sh --mode ${TEST_MODE}; EXIT_CODE=$?; if [ -d '/reports' ]; then cp report.xml report.html /reports/ 2>/dev/null || true; fi; exit $EXIT_CODE"]

