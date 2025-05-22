#!/bin/bash

# Test wildcard expansion

# Create dummy files
touch test_file_1.txt
touch test_file_2.c
touch another_file.txt

# Run the shell with a wildcard command and capture output
OUTPUT=$(echo "ls *.txt" | ./dsh)

# Clean up dummy files
rm test_file_1.txt test_file_2.c another_file.txt

# Check the output
# The output should contain "another_file.txt" and "test_file_1.txt"
# The order might vary, so check for presence of both
if echo "$OUTPUT" | grep -q "another_file.txt" && echo "$OUTPUT" | grep -q "test_file_1.txt"; then
    echo "Test wildcard expansion: PASSED"
else
    echo "Test wildcard expansion: FAILED"
    echo "Expected output to contain 'another_file.txt' and 'test_file_1.txt'"
    echo "Actual output:"
    echo "$OUTPUT"
    exit 1
fi

exit 0
