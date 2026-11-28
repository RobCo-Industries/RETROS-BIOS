# RETROS-BIOS

RETROS-BIOS is the first stage of the terminal chain of the RobCo suite. It runs bare metal on a Raspberry Pi in AArch32. The firmware loads `RETROS.IMG`. The BIOS tests the hardware, shows a boot screen in the style of the Fallout terminals, loads the second stage `/ROBCO/MFBOOT.SYS` from the SD card and hands off to it.

It is built on [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core), a submodule at `external/robco-core`. The boot contract is RobCo-Core's [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/9f45fcc1ba68915b3e0b0a9634544edc1d880e83/docs/spec/BOOT_PROTOCOL.md).

## Status

- Boots in QEMU on `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`.
- Runs a power-on self test, reads its settings from `/ROBCO/BOOT.CFG`, loads `MFBOOT.SYS` or the backup `MFBOOT.BAK` and hands off with the boot info.
- On a failure it shows the error and opens the emergency shell. A key in the key window opens it too. The shell has 12 commands and eight diagnostics.
- A stopped timer halts the BIOS in an idle loop that answers the maintenance port.
- Nothing is validated on real hardware. The checklist for a board is in [docs/TESTING.md](docs/TESTING.md#hardware-checklist).

## Build, run, test

You need `arm-none-eabi-gcc`, GNU make, Python 3, QEMU and mtools. The tests ran with QEMU 10.1.

```
git submodule update --init --recursive
make                      # build/retros.img and the test second stage
make run BOARD=raspi2b    # boot build/sd.img in QEMU, WINDOW=1 shows the screen
make test                 # host tests, then target tests on four boards
```

- [docs/USER_MANUAL.md](docs/USER_MANUAL.md): for a person with a Pi and a serial cable.
- [docs/BUILD.md](docs/BUILD.md): build targets and the SD card.
- [docs/BOOT.md](docs/BOOT.md): the boot flow, markers and error codes.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): files and memory use.
- [docs/TESTING.md](docs/TESTING.md): the tests.

## Lore and real values

The boot screen mixes lore text with real values. The second line, `RBIOS-4.02.08.00 52EE5.E7.E8`, and the copyright line are fixed lore text. The version, the memory size, the board and every POST result are real. `src/bootlines.c` holds every line and marks which is which.

## Licence

MIT. See [LICENSE](LICENSE).
