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

run_test "Echo command" "echo hello\nexit\n" "hello\nGoodbye!"

run_test "List directory" "ls -1 tests\nexit\n" "$(ls -1 tests)\nGoodbye!"

run_test "Invalid command" "nonexistingcmd\nexit\n" "command not found: nonexistingcmd\nGoodbye!"