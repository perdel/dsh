#!/bin/bash

SHELL_EXEC="./dsh"

OUTPUT_FILE="output.txt"
EXPECTED_FILE="expected.txt"

run_test() {
    echo "Test: $1"
    echo -e "$2" | $SHELL_EXEC > "$OUTPUT_FILE" 2>&1
    echo -e "$3" > "$EXPECTED_FILE"

    if diff -q "$OUTPUT_FILE" "$EXPECTED_FILE" >/dev/null; then
        echo "✅ Passed"
    else
        echo "❌ Failed"
        echo "Expected:"
        cat "$EXPECTED_FILE"
        echo "Got:"
        cat "$OUTPUT_FILE"
    fi
    echo
}

run_test "Simple Piping" \
    "ls -l tests | wc -l\nexit\n" \
    "$(ls -l tests | wc -l)\nGoodbye!"

run_test "Piping with Arguments" \
    "echo hello | grep hello\nexit\n" \
    "hello\nGoodbye!"

run_test "Multiple Pipes" \
    "ls -l | grep '^d' | wc -l\nexit\n" \
    "$(ls -l | grep '^d' | wc -l)\nGoodbye!\n"

run_test "Non-Existent Command in Pipeline" \
    "ls | nonexistingcmd | wc -l\nexit\n" \
    "sh: 1: nonexistingcmd: not found\nGoodbye!\n"

run_test "Piping with Input Redirection" \
    "cat < /etc/passwd | head -n 5\nexit\n" \
    "$(cat < /etc/passwd | head -n 5)\nGoodbye!\n"

run_test "Complex Pipe with Commands" \
    "echo -e 'apple\nbanana\ncarrot' | sort | tail -n 1\nexit\n" \
    "$(echo -e 'apple\nbanana\ncarrot' | sort | tail -n 1)\nGoodbye!\n"

rm "$OUTPUT_FILE" "$EXPECTED_FILE"