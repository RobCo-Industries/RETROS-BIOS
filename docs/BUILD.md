# Build

## Tools

- `arm-none-eabi-gcc` and binutils.
- GNU make, Python 3.
- QEMU with `qemu-system-arm` and `qemu-system-aarch64`, for `make run` and the target tests.
- mtools, for `make sdimage` and the tests that build cards.
- A host C compiler for the host tests. They use the address and undefined behaviour sanitizers when the compiler has them.

Fetch the submodule once:

```
git submodule update --init --recursive
```

## Targets

| Target | Result |
|---|---|
| `make` or `make all` | `build/retros.img` and the test second stage `build/stage2/stage2.img` |
| `make CI=1 all` | The same with `-Werror` |
| `make sdimage` | `build/sd.img`: a 64 MiB card with `RETROS.IMG`, `config.txt`, `ROBCO/BOOT.CFG` and a second stage as `ROBCO/MFBOOT.SYS`. `STAGE2=<file>` picks the second stage. The default is the test stage. |
| `make run BOARD=raspi2b` | Builds the card and boots it in QEMU with the console on this terminal. `WINDOW=1` opens the screen. Ctrl-A, then x, quits. |
| `make bootdir DIR=<path>` | The card files plus the Pi firmware, for a real card. `FIRMWARE=0` leaves the firmware out. A `ROBCO/BOOT.CFG` already in `DIR` stays. |
| `make test-host` | Host tests, shell tests and the pin check |
| `make test-target` | QEMU target tests. `BOARD=all` by default, or one board. |
| `make test` | `test-host`, then `test-target` |
| `make check-pins` | The CI workflow pins and the RobCo-Core doc links match the submodule, and the workflow guard passes |
| `make check-workflows` | RobCo-Core's workflow guard on `.github/workflows/` |
| `make clean` | Removes `build/` |

`BUILD=<dir>` builds somewhere other than `build/`.

## The image

`RETROS.IMG` is a rimg image, linked at `0x8000`, with the `BOOT` magic, subtype 5 (BIOS), ABI 2.0 and the name `RETROS`. RobCo-Core's flags build one ARMv6 image for every board: ARM1176, soft float, no `movw` or `movt`. The link fails when the image, its BSS and its stacks pass `0x00100000`. `tools/rimg.py` in RobCo-Core seals the CRC.

## The card

`boot/config.txt` holds the terminal chain settings from RobCo-Core's [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/300e5a30e9605319425d25dd4cb28a9f3d7b50a7/docs/spec/BOARD_SUPPORT.md#configtxt). The card gets the copy that `tools/mkbootdir.sh` writes, and `tests/host/sdimage_test.sh` checks that both are the same. `boot/BOOT.CFG` is the default `/ROBCO/BOOT.CFG`: the MFBootAgent entries from the spec and the BIOS defaults in `[bios]`.

The test second stage on the card only checks the handoff and stops. Replace it with a real MFBootAgent through `STAGE2=<file>`.
