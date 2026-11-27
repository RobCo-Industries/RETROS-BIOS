# Changelog

All notable changes to RETROS-BIOS are listed here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The BIOS checks its own image against its sealed CRC at boot with `rimg_self_check()`. It prints `selfcheck rc=<name> size=<n>`, adds a screen line when the check fails, and shows the result in `INFO`. The boot goes on.
- The emergency shell on the serial console: `HELP`, `INFO`, `POST`, `VOL`, `DIR`, `TYPE`, `MEM`, `BOOT`, `CONTINUE`, `DIAG`, `REBOOT` and `CLS`. It opens from the key window, after a boot error, and from `shell` on the maintenance port. Every command ends with a `shell_cmd` marker. A line holds 71 characters, so it fits one row after the prompt.
- `POST` in the shell finds a card put in after power-on. `POST` drops the volume of a pulled card. A volume whose unmount fails shows as `what=held`, and the next `POST` with a card that answers frees it.
- Diagnostics: `DIAG MEM`, `SD`, `VIDEO`, `AUDIO`, `TIMER`, `RNG`, `RTC`, `INPUT` and `ALL`, each with a `diag` marker. `DIAG TIMER` stamps the system timer inside the tick interrupt over 200 ticks.
- `shell` and `paging` on the maintenance port.
- `docs/USER_MANUAL.md`.
- A power-on self test: memory, video, timer, SD card, FAT volume, RNG, RTC and audio, with a table on screen and a marker per probe.
- A boot screen with the version, the memory size and the board, and two lore lines.
- `[bios]` settings in `/ROBCO/BOOT.CFG`: `delay_ms`, `beep` and `scheme`.
- A power-on beep and a key window.
- Loading `/ROBCO/MFBOOT.SYS` with `/ROBCO/MFBOOT.BAK` as the fallback, and the handoff with the boot info as BOOT_PROTOCOL.md says.
- Error lines and codes for each load failure, and an idle loop that answers `state`, `dump` and `reboot` on the maintenance port.
- A test second stage, host tests, QEMU target tests on four boards and CI through RobCo-Core's reusable workflow.

### Changed

- `make check-pins` runs RobCo-Core's workflow guard, `make check-workflows`, so CI fails on a `${{ }}` in a `run:` block or an unpinned `uses:`.
- RobCo-Core is now at `300e5a3`. Its `fs_unmount()` drops the volume of a pulled card, so `POST` after a card pull shows `FAT ABSENT no card` and `Boot volume: none` instead of a held volume.
- RobCo-Core is now at `25f36d8`. Its harness parks the spare cores of QEMU's raspi3b, so `TYPE` and `MEM` in `test_shell.py` keep the default timeout and lose the longer one added for the slow CI runner. No source file changed with the bump.
- The `fat` probe finds the boot volume with RobCo-Core's `fs_mount_boot()`. The rule is the same, with one addition: after the FAT entries, it tries the other entries, so a FAT volume behind a wrong type byte now boots. The error codes and lines stay the same.
- The board line and `INFO` take the board name from RobCo-Core's `board_model_name()`. Compute modules now read `Raspberry Pi Compute Module <n>`. Old-style codes name the model, such as `Raspberry Pi 1 Model B`, not `Raspberry Pi 1`. An unknown code reads `Raspberry Pi (unknown model)`. The Pi 4, 400, Compute Module 4 and 5 get their names.

- Rebuilt the BIOS on RobCo-Core, a submodule at `external/robco-core`. The prototype is gone.
- A boot error now opens the shell instead of halting. Only a stopped timer still halts, with `shell available=0`.
- A key in the key window opens the shell. The boot goes on with `CONTINUE`.

### Fixed

- A line pasted on the console runs whole, and several pasted lines run in order. RobCo-Core `f5bba36` leaves console bytes in the UART until its input queue has room.
- The BIOS now finds its boot volume on cards with more than one partition. It tries the FAT entries of the MBR in slot order and mounts the first one that holds `/ROBCO/MFBOOT.SYS` or `MFBOOT.BAK` as a non-empty file. Before, it mounted the first entry of any type, so a card with a non-FAT partition first did not boot. The `mount` marker and the POST row name the slot.
- A `delay_ms` with ten or more digits now clamps to 10000. It was rejected as bad, and the default of 1000 applied.
- With `delay_ms = 0` the BIOS now prints `key_window_end key=0` after `key_window ms=0`, so the window always has a closing marker.
- A stopped system timer now stops the boot with `E17 TIMER`. The timer probe runs third, after memory and video. After a stopped timer, no later probe runs: each shows `SKIPPED`, and no step waits on the timer. After a failed memory test, only the timer probe runs. The shell opens with a warning when the timer passes. When it is stopped too, the BIOS shows `E16 MEMTEST` and halts. Before, the BIOS went on to the audio probe, which waits on the timer and could hang.
