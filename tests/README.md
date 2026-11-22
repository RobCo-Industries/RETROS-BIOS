# RETROS-BIOS Test Suite

## `run_tests.sh`

Checks that:
- the ARM toolchain is available
- the BIOS builds for BCM2835, BCM2836 and BCM2837
- required source and header files exist
- the build has no warnings
- `build/kernel.elf` is produced and contains `kernel_main`
- `make clean` removes build output

These are build checks only. Nothing runs the BIOS code.

```bash
bash tests/run_tests.sh
```

### Install dependencies (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi
```

## Continuous Integration

`.github/workflows/ci.yml` builds all three targets, runs `run_tests.sh`,
checks the binary size and archives the images.
