# RETROS-BIOS Test Suite

This directory contains the test suite for RETROS-BIOS.

## Test Files

### `run_tests.sh`
Main test runner that performs:
- Toolchain availability checks
- Build tests for all platforms (BCM2835, BCM2836, BCM2837)
- Source and header file presence verification
- Compilation warning checks
- ELF file generation validation
- Clean target verification

**Usage:**
```bash
cd tests
./run_tests.sh
# Or from repository root:
# bash tests/run_tests.sh
```

### `test_memory.py`
Unit tests for memory management functions:
- `memset` - Fill memory with a value
- `memcpy` - Copy memory regions
- `memcmp` - Compare memory regions
- `strlen` - Calculate string length
- `strcmp` - Compare strings
- `strcpy` - Copy strings

These tests compile the functions in a host environment to verify correctness.

**Usage:**
```bash
cd tests
python3 test_memory.py
```

## Running Tests Locally

### Prerequisites
- ARM cross-compiler toolchain (`gcc-arm-none-eabi`)
- Python 3 (for unit tests)
- GCC (for compiling host-side tests)

### Install Dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi python3
```

### Run All Tests
```bash
# From tests directory
cd tests
./run_tests.sh
python3 test_memory.py

# Or from repository root
bash tests/run_tests.sh
python3 tests/test_memory.py
```

## Continuous Integration

Tests are automatically run on every pull request via GitHub Actions.

The CI workflow includes:
1. **Build and Test Job**
   - Build for all platforms
   - Run unit tests
   - Run integration tests
   - Check binary size limits
   - Archive build artifacts

2. **Static Analysis Job**
   - Check for trailing whitespace
   - Find TODO/FIXME comments
   - Verify header guards
   - Validate code structure

3. **Documentation Check Job**
   - Verify documentation files exist
   - Check README content

## Test Results

Tests output color-coded results:
- ✓ (green) - Test passed
- ✗ (red) - Test failed

## Adding New Tests

### Adding Build Tests
Edit `run_tests.sh` and add new test cases following the existing pattern.

### Adding Unit Tests
Create a new Python script in this directory following the pattern in `test_memory.py`.

### Updating CI Workflow
Edit `.github/workflows/ci.yml` to add new CI checks or modify existing ones.

## Test Coverage

Current test coverage:
- ✓ Build system (all platforms)
- ✓ Memory functions (unit tests)
- ✓ String functions (unit tests)
- ✓ Source file presence
- ✓ Binary size limits
- ✓ Static analysis
- ✓ Documentation presence

## Known Limitations

- Hardware-specific functions (UART, GPIO, MMC, etc.) cannot be easily tested without actual hardware or emulation
- Integration tests are limited to build-time checks
- No runtime tests on actual Raspberry Pi hardware in CI

## Future Improvements

- [ ] Add QEMU-based integration tests
- [ ] Implement mock hardware for testing drivers
- [ ] Add code coverage reporting
- [ ] Performance benchmarks
- [ ] Automated regression testing
