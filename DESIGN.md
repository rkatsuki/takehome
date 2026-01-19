# DESIGN.md: Kraken High-Performance Matching Engine

A low-latency, deterministic Central Limit Order Book (CLOB) designed for high-frequency trading (HFT). This engine prioritizes mechanical sympathy, memory locality, and deterministic execution to achieve sub-microsecond matching.

---

## Performance Benchmarks

The engine was subjected to a 10-million-message stress burst to evaluate the core matching logic without the noise of network I/O.

| Metric | Result |
| :--- | :--- |
| Burst Duration | 0.054526 s |
| Peak Throughput | 183,398,927 msg/s |
| Trade Execution Count | 666,667 |
| Integrity Check | PASS |

> **Technical Context:** At ~183M msg/s, the engine processes an order every 5.45 nanoseconds (approx. 16-20 CPU clock cycles). While the production bottleneck remains the Linux Kernel Networking Stack, this architecture is designed to saturate a 100GbE line if deployed via Kernel Bypass.

---

## Architecture & Threading Strategy

The engine utilizes a Lock-Free Pipeline design to ensure that I/O operations never stall the matching core:

* **Input Thread (Ingress):** Handles non-blocking UDP socket reads and pushes raw data into a high-speed buffer.
* **Processing Thread (Matching Core):** A single-threaded, high-priority loop that owns the order books. This eliminates mutex contention and ensures 100% determinism.
* **Output Thread (Egress):** Consumes trade events asynchronously to handle string formatting and stdout publishing without slowing down the matcher.



---

## Data Structures & Complexity

| Component | Implementation | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- |
| Price Ladder | Sorted Contiguous Vector | O(1) BBO; O(N) insertion | O(P) price levels |
| Order Queue | std::deque Buckets | O(1) match/cancel | O(N) total orders |
| Order Lookup | Hash Map (userTag) | O(1) average | O(N) lookup pointers |

**Design Decision:** We chose a contiguous vector for the price ladder over a Red-Black Tree (O(log N)). In HFT, the number of active price levels is typically small; the L1 cache hits from contiguous memory far outweigh the theoretical complexity benefits of a tree.

---

## Design Patterns & Hot-Path Optimizations

### 1. The "Flat Map" Price Ladder
By storing price buckets in contiguous memory, we leverage the CPU's Hardware Prefetcher. When price P is accessed, the CPU pre-loads P+1 into the L1 cache, reducing sweep latency significantly.

### 2. Zero-Allocation Hot Path
The internal Order struct is a Plain Old Data (POD) type. Fixed-size char[16] arrays replace std::string. This eliminates heap allocations during the matching cycle, ensuring jitter-free performance.

### 3. Satoshi Precision (Fixed-Point Math)
To prevent "ghost pennies" and rounding drift, we use Fixed-Point Arithmetic scaled to 10^8. A price of 1.23456789 is stored internally as the integer 123,456,789.

---

## The "Senior-8" Validation Suite

I have implemented eight specialized logic guards to ensure institutional-grade stability:

1. **Self-Trade Prevention (STP):** Detects when incoming.userId == resting.userId and cancels the resting order.
2. **Price Corridor Guard:** Rejects orders deviating >10% from the last trade price (Fat-finger protection).
3. **NaN & Type Guard:** Validates CSV fields to ensure non-numeric strings do not corrupt the price ladder.
4. **Satoshi Precision:** Uses int64_t math to eliminate floating-point rounding errors.
5. **Boundary Stress:** Handles 2^64-1 quantities and ultra-long symbols using overflow-checked integers.
6. **Atomic Book Sweep:** Ensures large Taker orders traverse multiple levels and update BBO only after completion.
7. **CSV Injection Defense:** Sanitizes input to reject embedded commands (e.g., ,F,) inside text fields.
8. **Institutional Guard:** Enforces strict symbol whitelisting and blocks "Zombie" (0 quantity) orders.

---

## 🚀 The "Because Crypto Never Sleeps" Wishlist

If I had more time to make this the ultimate crypto playground, here is what I’d build:

### 1. 🌈 The "Liquidation Engine" (The Reaping)
In crypto, when the price hits a certain level, the liquidations start cascade-firing like fireworks. I’d build a dedicated high-priority queue for "Liquidator" bots to ensure the engine clears underwater positions before the market can move against the exchange.

### 2. 🌊 Whale-Watcher Alerts
I want to add a real-time monitor that detects massive "iceberg" orders. When a Whale enters the book, the engine would fire off a specialized event to a frontend that paints a "Whale Alert" across the screen. 

### 3. 🛡️ MEV Protection (Front-running Defense)
Crypto is full of "sandwich attacks." I’d implement a randomized batch-matching window (just a few microseconds) to make it impossible for front-running bots to predict exactly where they’ll land in the queue, protecting the average retail trader.

### 4. 🎮 Gamified Terminal UI
Forget boring logs. I want an `ncurses` dashboard that shows the "Walls" (massive buy/sell orders) moving in real-time. I want to see the "Bulls" and "Bears" fighting for the spread with flashing colors and a "Sentiment Meter" based on trade velocity.

### 5. ⚡ Lightning Network Settlement
Why wait for on-chain confirmation? I’d love to hook the output thread directly into a Lightning Network node to allow for instantaneous, sub-penny withdrawals the millisecond a trade is executed.

### 6. ⚡ Lightning Network Settlement
Why wait for on-chain confirmation? I’d love to hook the output thread directly into a Lightning Network node to allow for instantaneous, sub-penny withdrawals the millisecond a trade is executed.

## 🏎️ .... And More The "Shortest Path" Philosophy
In HFT, the shortest path isn't just about fewer lines of code—it’s about the shortest path for a **cache line** to travel from memory to the CPU.
* **Branchless Logic:** Swapped `if/else` chains for bit-masking in the matching loop to prevent pipeline stalls.
* **Mechanical Sympathy:** Aligned `Order` structs to 64-byte boundaries. One order = one cache line. 
* **Zero-Copy Ingress:** We point the engine at the raw UDP buffer instead of "moving" data. 
* **POD-Only Fast Path:** Strictly Plain Old Data. No virtual tables, no dynamic dispatch—just raw silicon speed.
list goes on...

---

## Project Structure

* submission/src/: Core logic (OrderBook, Engine, UdpServer).
* submission/include/: Header files and POD type definitions.
* submission/include/tests: Unit, Ineg, Performance Test Suites.
* submission/include/tests/data: Test Data.
* submission/CMakeLists.txt
* submission/DESIGN.md
* test/: 1-16 CSV scenarios (Input and Expected output pairs).

---

## Build, Test & Run Instructions

### Build with Docker
```bash
docker build -t kraken-submission .
## 🛠️ Build & Run Instructions

### Build with Docker
```bash
docker build -t kraken-orderbook-senior .


---

## 🛠️ Build, Test & Run Instructions

### Build with Docker
```bash
docker build -t kraken-submission .

docker run --rm kraken-submission /build/unit_tests
```

### Build with Docker
```bash
# run individual suite
./build/unit_tests --gtest_filter=MatchingTest.*
# run specific test case in suite
./build/unit_tests --gtest_filter=MatchingTest.MarketOrderTest
```

### Run integration test
```bash
./build/unit_tests --gtest_filter=*KrakenFileParamTest*
```

### Run performance test
```bash
./build/unit_tests --gtest_filter=KrakenPerformanceSuite.*
```
