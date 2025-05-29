#!/bin/bash

# Set strict mode
set -euo pipefail

# Source the test helper functions (assuming this exists based on other tests)
# If test_helper.sh doesn't exist, you may need to create it or adjust this line.
. tests/test_helper.sh

# Define the shell executable
DSH="./dsh"

echo "--- Testing Quoting and Escaping ---"

# Test single quotes
assert_output "echo 'hello world'" "hello world" "Single quotes preserve literal string"

# Test double quotes
assert_output "echo \"hello world\"" "hello world" "Double quotes preserve literal string"

# Test escaped space
assert_output "echo hello\\ world" "hello world" "Escaped space"

# Test escaped double quote within double quotes
assert_output "echo \"hello \\\"world\\\"\"" "hello \"world\"" "Escaped double quote within double quotes"

# Test escaped single quote within single quotes (should be literal)
assert_output "echo 'hello \\'world\\''" "hello \\'world\\'" "Escaped single quote within single quotes (literal)"

# Test escaped backslash
assert_output "echo hello\\\\world" "hello\\world" "Escaped backslash"

# Test mixed quotes and escapes
assert_output "echo \"hello 'world'\" 'hello \"world\"' hello\\ \\'world\\'" "hello 'world' hello \"world\" hello 'world'" "Mixed quotes and escapes"

# Test quotes in filenames for output redirection
OUTPUT_FILE_QUOTED="output file with spaces.txt"
assert_output "echo 'quoted filename test' > \"${OUTPUT_FILE_QUOTED}\"" "" "Output redirection with quoted filename"
assert_output "cat \"${OUTPUT_FILE_QUOTED}\"" "quoted filename test" "Read from quoted filename"
rm -f "${OUTPUT_FILE_QUOTED}"

# Test escaped spaces in filenames for output redirection
OUTPUT_FILE_ESCAPED="output\\ file\\ with\\ spaces.txt"
assert_output "echo 'escaped filename test' > ${OUTPUT_FILE_ESCAPED}" "" "Output redirection with escaped filename"
assert_output "cat ${OUTPUT_FILE_ESCAPED}" "escaped filename test" "Read from escaped filename"
rm -f output\ file\ with\ spaces.txt # Need to remove the actual file name

# Test quotes in filenames for input redirection
INPUT_FILE_QUOTED="input file with spaces.txt"
echo "quoted input test" > "${INPUT_FILE_QUOTED}"
assert_output "cat < \"${INPUT_FILE_QUOTED}\"" "quoted input test" "Input redirection with quoted filename"
rm -f "${INPUT_FILE_QUOTED}"

# Test escaped spaces in filenames for input redirection
INPUT_FILE_ESCAPED="input\\ file\\ with\\ spaces.txt"
echo "escaped input test" > input\ file\ with\ spaces.txt
assert_output "cat < ${INPUT_FILE_ESCAPED}" "escaped input test" "Input redirection with escaped filename"
rm -f input\ file\ with\ spaces.txt

# Test quotes/escapes with wildcards (should not expand within quotes)
assert_output "echo '*'" "*" "Wildcard within single quotes (literal)"
assert_output "echo \"*\"" "*" "Wildcard within double quotes (literal)"
assert_output "echo \\*" "*" "Escaped wildcard (literal)"

# Test quotes/escapes with built-in commands (cd)
TEST_DIR="test dir with spaces"
mkdir -p "${TEST_DIR}"
assert_output "cd \"${TEST_DIR}\" && pwd" "$(pwd)/${TEST_DIR}" "cd with quoted directory name"
cd - > /dev/null # Go back to previous directory
rmdir "${TEST_DIR}"

TEST_DIR_ESCAPED="test\\ dir\\ with\\ spaces"
mkdir -p test\ dir\ with\ spaces
assert_output "cd ${TEST_DIR_ESCAPED} && pwd" "$(pwd)/test dir with spaces" "cd with escaped directory name"
cd - > /dev/null # Go back to previous directory
rmdir test\ dir\ with\ spaces

echo "--- Quoting and Escaping Tests Complete ---"
