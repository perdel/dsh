#!/bin/bash

# Helper functions for dsh tests

# Define the shell executable (can be overridden by test scripts)
DSH="./dsh"

# Function to assert the output of a command run through dsh
# Usage: assert_output "command string" "expected output" "description"
assert_output() {
    local command_string="$1"
    local expected_output="$2"
    local description="$3"

    # Run the command through dsh and capture stdout
    # Use printf "%s" to avoid adding an extra newline if the command output doesn't end with one
    local actual_output=$(printf "%s\n" "$command_string" | ${DSH} 2>&1)
    local exit_status=$?

    if [ "$actual_output" = "$expected_output" ] && [ "$exit_status" -eq 0 ]; then
        echo "PASS: ${description}"
    else
        echo "FAIL: ${description}"
        echo "  Command: ${command_string}"
        echo "  Expected: '${expected_output}'"
        echo "  Actual:   '${actual_output}'"
        echo "  Exit Status: ${exit_status}"
        # Exit immediately on failure to prevent cascading errors
        exit 1
    fi
}