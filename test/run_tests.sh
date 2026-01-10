#!/bin/bash

# Kraken Interview Test Runner
# Supports multiple input modes: stdin (default) or UDP

BIN="/kraken_submission/build/kraken_submission"
TEST_DIR=$(dirname "$0")
REPORT="$TEST_DIR/report.xml"

# Default configuration
INPUT_MODE="stdin"
UDP_PORT="1234"

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --mode)
            INPUT_MODE="$2"
            shift 2
            ;;
        --port)
            UDP_PORT="$2"
            shift 2
            ;;
        --bin)
            BIN="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

# Track test results
failed_tests=0
passed_tests=0
total_time=0
test_results=()

echo ""
echo "🚀 Running Test Suite"
echo "════════════════════════════════════════════════"
echo "   Mode: $INPUT_MODE"
if [ "$INPUT_MODE" = "udp" ]; then
    echo "   Port: $UDP_PORT"
fi
echo "════════════════════════════════════════════════"
echo ""

# Start JUnit XML
start_time=$(date +%s)

# For UDP mode, start binary once and reuse for all tests
if [ "$INPUT_MODE" = "udp" ]; then
    mkfifo /tmp/output_fifo 2>/dev/null || true
    timeout -k 2s 60s "$BIN" 2>/tmp/stderr.txt > /tmp/output_fifo &
    BIN_PID=$!
    
    # Read output in background
    cat /tmp/output_fifo > /tmp/all_output.txt &
    CAT_PID=$!
    
    # Brief delay for socket binding
    sleep 0.3
fi

for testcase in /test/*; do
    [ -d "${testcase}" ] || continue
    dirname="$(basename "${testcase}")"
    
    # Skip if not a numbered directory
    [[ ! "$dirname" =~ ^[0-9]+$ ]] && continue

    echo "Test Case: $dirname"
    
    test_start=$(date +%s.%N)
    
    # Run test based on input mode
    if [ "$INPUT_MODE" = "stdin" ]; then
        # Standard stdin mode
        timeout -k 2s 5s "$BIN" < /test/$dirname/in.csv > /test/test_output.csv 2>/tmp/stderr.txt
        exit_code=$?
        
    elif [ "$INPUT_MODE" = "udp" ]; then
        # UDP mode - send to already-running binary
        # Count expected output lines
        expected_lines=$(wc -l < /test/$dirname/out.csv)
        before_lines=$(wc -l < /tmp/all_output.txt 2>/dev/null || echo 0)
        
        # Send test input
        while IFS= read -r line || [ -n "$line" ]; do
            echo "$line" > /dev/udp/127.0.0.1/$UDP_PORT 2>/dev/null
        done < /test/$dirname/in.csv
        
        # Wait for expected output lines (with timeout)
        timeout_count=0
        while [ $timeout_count -lt 20 ]; do
            current_lines=$(wc -l < /tmp/all_output.txt 2>/dev/null || echo 0)
            received=$((current_lines - before_lines))
            
            if [ $received -ge $expected_lines ]; then
                break
            fi
            
            sleep 0.1
            timeout_count=$((timeout_count + 1))
        done
        
        # Extract output for this test
        tail -n +$((before_lines + 1)) /tmp/all_output.txt | head -n $expected_lines > /test/test_output.csv
        
        exit_code=0
    else
        echo "  ❌ ERROR - Unknown input mode: $INPUT_MODE"
        exit_code=1
    fi
    
    test_end=$(date +%s.%N)
    test_time=$(echo "$test_end - $test_start" | bc)
    
    # Compare output with expected
    diff /test/test_output.csv /test/$dirname/out.csv > /tmp/diff.txt 2>&1
    diff_result=$?
    
    # Store result for JUnit XML
    if [ $exit_code -ne 0 ]; then
        echo "  ❌ FAILED - Process exited with code $exit_code"
        if [ -s /tmp/stderr.txt ]; then
            echo "  Error output:"
            cat /tmp/stderr.txt | sed 's/^/    /'
        fi
        test_results+=("FAIL:$dirname:$test_time:Process exited with code $exit_code")
        failed_tests=$((failed_tests+1))
    elif [ $diff_result -ne 0 ]; then
        echo "  ❌ FAILED - Output does not match expected"
        echo "  Differences:"
        cat /tmp/diff.txt | head -20 | sed 's/^/    /'
        test_results+=("FAIL:$dirname:$test_time:Output mismatch")
        failed_tests=$((failed_tests+1))
    else
        echo "  ✅ PASSED (${test_time}s)"
        test_results+=("PASS:$dirname:$test_time")
        passed_tests=$((passed_tests+1))
    fi
    echo ""
    
    # Cleanup
    rm -f /test/test_output.csv /tmp/stderr.txt /tmp/nc_stderr.txt
done

# Cleanup UDP mode resources
if [ "$INPUT_MODE" = "udp" ]; then
    # Kill the binary and cat process
    kill $BIN_PID 2>/dev/null || true
    kill $CAT_PID 2>/dev/null || true
    wait $BIN_PID 2>/dev/null || true
    wait $CAT_PID 2>/dev/null || true
    
    # Clean up
    rm -f /tmp/output_fifo /tmp/all_output.txt /tmp/udp_send_err.txt
fi

end_time=$(date +%s)
total_time=$((end_time - start_time))
total_tests=$((passed_tests + failed_tests))

# Generate JUnit XML Report
cat > "$REPORT" << EOF
<?xml version="1.0" encoding="UTF-8"?>
<testsuites>
  <testsuite name="InterviewTests" tests="$total_tests" failures="$failed_tests" errors="0" time="$total_time" timestamp="$(date -Iseconds)">
EOF

for result in "${test_results[@]}"; do
    IFS=':' read -r status name time message <<< "$result"
    if [ "$status" = "PASS" ]; then
        echo "    <testcase name=\"test_$name\" classname=\"InterviewTests\" time=\"$time\"/>" >> "$REPORT"
    else
        cat >> "$REPORT" << TESTCASE
    <testcase name="test_$name" classname="InterviewTests" time="$time">
      <failure message="Test failed" type="AssertionError">$message</failure>
    </testcase>
TESTCASE
    fi
done

cat >> "$REPORT" << EOF
  </testsuite>
</testsuites>
EOF

# Generate HTML report from JUnit XML
HTML_REPORT="${TEST_DIR}/report.html"
if command -v junit2html &> /dev/null; then
    junit2html "$REPORT" "$HTML_REPORT" 2>/dev/null || true
fi

# Print summary
echo "════════════════════════════════════════════════"
echo "📊 TEST SUMMARY"
echo "════════════════════════════════════════════════"
echo ""
echo "  Total Tests:  $total_tests"
echo "  ✅ Passed:     $passed_tests"
echo "  ❌ Failed:     $failed_tests"
echo "  ⏱️  Duration:   ${total_time}s"
echo ""
if [ $failed_tests -eq 0 ]; then
    echo "  🎉 All tests passed! Congratulations!"
else
    echo "  ⚠️  Some tests failed. Please review."
fi
echo ""
echo "════════════════════════════════════════════════"
echo "📄 Reports generated:"
echo "   - JUnit XML: $REPORT"
if [ -f "$HTML_REPORT" ]; then
    echo "   - HTML:      $HTML_REPORT"
fi
echo ""

if [ $failed_tests -eq 0 ]; then
    exit 0
else
    exit 1
fi

