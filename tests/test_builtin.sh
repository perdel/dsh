#!/bin/bash

SHELL_EXEC="./dsh"

OUTPUT_FILE="output.txt"
EXPECTED_FILE="expected.txt"

run_test() {
    echo "Test: $1"
    echo -e "$2" | $SHELL_EXEC > "$OUTPUT_FILE" 2>&1
    echo -e "$3" > "$EXPECTED_FILE"

    if diff -u "$EXPECTED_FILE" "$OUTPUT_FILE"; then
        echo "✅ Passed"
    else
        echo "❌ Failed"
        echo "Expected:"
        cat "$EXPECTED_FILE"
        echo "Got:"
        cat "$OUTPUT_FILE"
    fi

    rm -f "$OUTPUT_FILE" "$EXPECTED_FILE"
    echo
}

run_test "Empty Input" "\nexit\n" "Goodbye!"

run_test "Built-in cd" "cd /tmp\npwd\nexit\n" "/tmp\nGoodbye!"

run_test "Built-in exit" "exit\n" "Goodbye!"

# Test redirection
echo "This is test input." > test_input.txt

run_test "Output Redirection (>)" "echo 'hello world' > output_redirect.txt\ncat output_redirect.txt\nexit\n" "hello world\nGoodbye!"
rm -f output_redirect.txt

run_test "Append Redirection (>>)" "echo 'line 1' > append_redirect.txt\necho 'line 2' >> append_redirect.txt\ncat append_redirect.txt\nexit\n" "line 1\nline 2\nGoodbye!"
rm -f append_redirect.txt

run_test "Input Redirection (<)" "cat < test_input.txt\nexit\n" "This is test input.\nGoodbye!"
rm -f test_input.txt

run_test "Combined Redirection (< and >)" "cat < test_input.txt > combined_redirect.txt\ncat combined_redirect.txt\nexit\n" "This is test input.\nGoodbye!"
rm -f combined_redirect.txt
