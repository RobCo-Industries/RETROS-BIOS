#!/bin/bash
# Test runner for RETROS-BIOS
# Runs compilation tests and basic sanity checks

# Don't exit on first error - we want to run all tests
# set -e

# Change to repository root directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$SCRIPT_DIR/.."

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

# Function to print test results
print_test() {
    local name=$1
    local result=$2
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓${NC} $name"
        ((TESTS_PASSED++))
    else
        echo -e "${RED}✗${NC} $name"
        ((TESTS_FAILED++))
    fi
}

echo "======================================"
echo "RETROS-BIOS Test Suite"
echo "======================================"
echo ""

# Test 1: Check toolchain availability
echo "Testing toolchain availability..."
if command -v arm-none-eabi-gcc &> /dev/null; then
    print_test "ARM toolchain available" "PASS"
else
    print_test "ARM toolchain available" "FAIL"
    echo "Please install arm-none-eabi toolchain"
    exit 1
fi

# Test 2: Build for BCM2835
echo ""
echo "Testing BCM2835 build..."
if make clean > /dev/null 2>&1 && make TARGET=BCM2835 > /dev/null 2>&1; then
    print_test "BCM2835 build" "PASS"
    SIZE=$(stat -f%z build/kernel.img 2>/dev/null || stat -c%s build/kernel.img 2>/dev/null)
    echo "  Binary size: $SIZE bytes"
    if [ "$SIZE" -lt 100000 ]; then
        print_test "BCM2835 binary size reasonable (<100KB)" "PASS"
    else
        print_test "BCM2835 binary size reasonable (<100KB)" "FAIL"
    fi
else
    print_test "BCM2835 build" "FAIL"
fi

# Test 3: Build for BCM2836
echo ""
echo "Testing BCM2836 build..."
if make clean > /dev/null 2>&1 && make TARGET=BCM2836 > /dev/null 2>&1; then
    print_test "BCM2836 build" "PASS"
    SIZE=$(stat -f%z build/kernel.img 2>/dev/null || stat -c%s build/kernel.img 2>/dev/null)
    echo "  Binary size: $SIZE bytes"
else
    print_test "BCM2836 build" "FAIL"
fi

# Test 4: Build for BCM2837
echo ""
echo "Testing BCM2837 build..."
if make clean > /dev/null 2>&1 && make TARGET=BCM2837 > /dev/null 2>&1; then
    print_test "BCM2837 build" "PASS"
    SIZE=$(stat -f%z build/kernel.img 2>/dev/null || stat -c%s build/kernel.img 2>/dev/null)
    echo "  Binary size: $SIZE bytes"
else
    print_test "BCM2837 build" "FAIL"
fi

# Test 5: Check for required source files
echo ""
echo "Testing source file presence..."
REQUIRED_FILES=(
    "src/boot.S"
    "src/main.c"
    "src/uart.c"
    "src/framebuffer.c"
    "src/gpio.c"
    "src/timer.c"
    "src/mmc.c"
    "src/memory.c"
)

for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        print_test "Source file: $file" "PASS"
    else
        print_test "Source file: $file" "FAIL"
    fi
done

# Test 6: Check for required header files
echo ""
echo "Testing header file presence..."
REQUIRED_HEADERS=(
    "include/hardware.h"
    "include/uart.h"
    "include/framebuffer.h"
    "include/gpio.h"
    "include/timer.h"
    "include/mmc.h"
    "include/memory.h"
)

for file in "${REQUIRED_HEADERS[@]}"; do
    if [ -f "$file" ]; then
        print_test "Header file: $file" "PASS"
    else
        print_test "Header file: $file" "FAIL"
    fi
done

# Test 7: Check linker script exists
echo ""
echo "Testing build system files..."
if [ -f "linker.ld" ]; then
    print_test "Linker script exists" "PASS"
else
    print_test "Linker script exists" "FAIL"
fi

if [ -f "Makefile" ]; then
    print_test "Makefile exists" "PASS"
else
    print_test "Makefile exists" "FAIL"
fi

# Test 8: Check for warnings in compilation
echo ""
echo "Testing for compilation warnings..."
if make clean > /dev/null 2>&1 && make 2>&1 | grep -i warning; then
    print_test "No compilation warnings" "FAIL"
else
    print_test "No compilation warnings" "PASS"
fi

# Test 9: Verify ELF file is created
echo ""
echo "Testing ELF file generation..."
make clean > /dev/null 2>&1
make > /dev/null 2>&1
if [ -f "build/kernel.elf" ]; then
    print_test "ELF file generated" "PASS"
    # Check if symbols are present
    if arm-none-eabi-nm build/kernel.elf | grep -q "kernel_main"; then
        print_test "kernel_main symbol present" "PASS"
    else
        print_test "kernel_main symbol present" "FAIL"
    fi
else
    print_test "ELF file generated" "FAIL"
fi

# Test 10: Clean build test
echo ""
echo "Testing clean target..."
make clean > /dev/null 2>&1
if [ ! -d "build" ] || [ -z "$(ls -A build 2>/dev/null)" ]; then
    print_test "Clean removes build artifacts" "PASS"
else
    print_test "Clean removes build artifacts" "FAIL"
fi

# Summary
echo ""
echo "======================================"
echo "Test Summary"
echo "======================================"
echo -e "${GREEN}Passed: $TESTS_PASSED${NC}"
if [ $TESTS_FAILED -gt 0 ]; then
    echo -e "${RED}Failed: $TESTS_FAILED${NC}"
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi
