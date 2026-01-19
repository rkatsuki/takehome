# Order Book Programming Exercise - Senior Level

Welcome to the deep waters of the Kraken challenge! 🦑

Implement a **multi-threaded** order book system that:
- Accepts input via **UDP protocol**
- Maintains multiple price-time order books (one per symbol)
- **Matches and trades** orders that cross the book
- Supports **market orders** (price = 0) with IOC (Immediate or Cancel) semantics
- Handles **partial quantity matches**

## How We Will Test Your Submission

Your solution will be evaluated using these commands:

```bash
docker build -t kraken-orderbook-senior .
docker run --rm kraken-orderbook-senior
```

**Requirements:**
- Must produce a binary named `kraken_submission`
- Must listen on **UDP port 1234** (hardcoded)
- Must accept CSV commands via UDP
- Must write CSV output to **stdout**
- **All provided test cases must pass**

💡 **Note**: Please don't modify the test files - we'll be running your solution against additional test cases in the same format.

## Requirements

### Input - UDP Protocol

Your program accepts input via **UDP on port 1234** (hardcoded).

The input uses CSV format with three command types:

**New Order:**
```csv
N, userId, symbol, price, quantity, side, userOrderId
```

**Cancel Order:**
```csv
C, userId, userOrderId
```

**Flush (reset all order books):**
```csv
F
```

**Market Orders:** When `price = 0`, the order is a market order that executes immediately at the best available price(s). Any unmatched portion is canceled (IOC - Immediate or Cancel).

### Order Book Logic

The order book uses **price-time priority**.

**Market Orders (price = 0):**
- Execute immediately against the opposite side at best available price(s)
- Any unmatched portion is canceled (IOC - Immediate or Cancel)
- Do not join the book

**Limit Orders (price > 0):**
- If they cross the book, match immediately (partial matches allowed)
- Any remaining quantity joins the book in price-time priority
- Sit on the book until matched or canceled

**Partial Matches:**
- Orders can be partially filled
- Multiple trades may result from a single order
- Remaining quantity stays on book (for limit orders)

### Multi-threading Requirements

Your program **must** use separate threads for:

1. **Input Thread**: Buffer incoming UDP messages
2. **Processing Thread**: Process orders and maintain book state
3. **Output Thread**: Publish results to stdout

This architecture ensures network I/O doesn't block order processing.

### Output - stdout

Your program publishes to **stdout** using CSV format:

**Order acknowledgement:**
```csv
A, userId, userOrderId
```

**Cancel acknowledgement:**
```csv
C, userId, userOrderId
```

**Trade (matched orders):**
```csv
T, userIdBuy, userOrderIdBuy, userIdSell, userOrderIdSell, price, quantity
```

**Top of book change:**
```csv
B, side (B or S), price, totalQuantity
```
- Use `-` for price and totalQuantity when a side is eliminated

**Important:** Output must be thread-safe. Multiple threads writing to stdout must not interleave.

### Test Scenarios

The test suite includes 16 scenarios covering:
- Balanced book operations
- Market orders (price = 0)
- Partial fills
- Multiple trades from single order
- Limit orders that cross and partially fill
- Multi-symbol books
- Cancellations
- Edge cases

**A fun twist:**
- **Odd-numbered scenarios (1, 3, 5, 7, 9, 11, 13, 15)** have provided expected outputs - use these to verify your logic
- **Even-numbered scenarios (2, 4, 6, 8, 10, 12, 14, 16)** have empty output files - it's your turn!
  - Generate the correct outputs for these scenarios
  - This shows us you understand the order book logic deeply
  - Include your generated outputs in your submission

## Build Requirements

Your submission must:
- Produce a binary named `kraken_submission`
- Listen on UDP port 1234 (hardcoded)
- Write output to stdout
- Work in the provided Docker environment (Ubuntu 24.04, x64)

## Testing

### Automated Testing

Run tests using Docker (works on all platforms):

```bash
# Build and run tests (reports saved to ./reports/)
docker build -t kraken-orderbook-senior .
docker run --rm -v "$(pwd)/reports:/reports" kraken-orderbook-senior
```

Test results are displayed in the terminal and saved to `./reports/`.

The test framework will:
1. Start your program listening on port 1234
2. Send test data via UDP
3. Collect output from stdout
4. Compare with expected output

### Manual Testing

**Inside Docker (recommended):**
```bash
# Build the Docker image
docker build -t kraken-orderbook-senior .

# Run a shell inside the container
docker run --rm -it --entrypoint /bin/bash kraken-orderbook-senior

# Inside container - start your program
cmake --build build 

/kraken_submission/build/kraken_submission &

# Send test data (requires BSD netcat)
cat /test/1/in.csv | nc -u 127.0.0.1 1234
```

**Local development:**
```bash
# Build your solution
mkdir build && cd build
cmake .. && cmake --build .

# Run (listens on port 1234)
./kraken_submission

# In another terminal, send test data (requires BSD netcat):
cat test/1/in.csv | nc -u 127.0.0.1 1234
```

## Submission Requirements

Your submission should include:

### 1. submission/ folder
- All source files in `submission/src/`
- Build configuration (CMakeLists.txt or Cargo.toml)
- Well-structured, readable code

### 2. test/ folder
- All provided test cases
- **Expected outputs for even-numbered scenarios** (that you generated)
- Any additional test cases you created

### 3. DESIGN.md

Create a `DESIGN.md` file explaining:
- **Architecture**: Thread design, synchronization strategy
- **Data structures**: Order book implementation, [time complexity](https://en.wikipedia.org/wiki/Time_complexity) and [space complexity](https://en.wikipedia.org/wiki/Space_complexity)
- **Design decisions**: Trade-offs made, why you chose specific approaches
- **Project structure**: File organization, key components
- **Improvements**: What you'd do with more time

## Tips

1. **Start simple**: Get basic UDP input/output working first
2. **Test incrementally**: Run the Docker tests frequently
3. **Generate even outputs early**: Don't leave this to the end
4. **Handle threading carefully**: Use proper synchronization (mutexes, channels)
5. **Document as you go**: Write DESIGN.md while implementing

## Creating the Submission Bundle

When complete:

1. **Ensure ALL odd-numbered tests pass** - Run the Docker tests to verify
2. **Generate correct outputs for even-numbered scenarios** - Include in `test/` folder
3. Verify your submission contains:
   - `submission/` folder with your code
   - `test/` folder with all test cases (including your even-numbered outputs)
   - `DESIGN.md` with architecture documentation
4. Create a git bundle:
   ```bash
   git add --all
   git commit -m "Complete senior order book implementation"
   git bundle create orderbook_<yourname>.bundle master
   ```
5. Send the `.bundle` file to your recruiter

## Important Notes

- ✅ **All odd-numbered test cases must pass** - This is the foundation we build on
- 📝 **Even-numbered outputs must be provided** - Shows us your understanding
- 💼 Please don't submit proprietary code or code from other companies
- 🔒 Please don't upload your solution to public repositories
- 📦 Your bundle should contain: `submission/`, `test/`, and `DESIGN.md`
- 🧪 Test thoroughly in the Docker environment before submitting - it's your best friend!

---

**Good luck, and may your threads stay synchronized! 🦑**

This is a challenging problem that will demonstrate your systems programming skills. Like the Kraken itself, it's multi-armed and ready to test your abilities. Dive deep, navigate carefully, and show us what you've got!
