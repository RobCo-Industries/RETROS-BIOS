# Changelog

All notable changes to RETROS-BIOS are listed here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Rebuilt the BIOS on RobCo-Core, a submodule at `external/robco-core`. The prototype is gone.

### Added

- A power-on self test: memory, timer, video, SD card, FAT volume, RNG, RTC and audio, with a table on screen and a marker per probe.
- A boot screen with the version, the memory size and the board, and two lore lines.
- `[bios]` settings in `/ROBCO/BOOT.CFG`: `delay_ms`, `beep` and `scheme`.
- A power-on beep and a key window. A key only prints that the emergency shell is not built yet.
- Loading `/ROBCO/MFBOOT.SYS` with `/ROBCO/MFBOOT.BAK` as the fallback, and the handoff with the boot info as BOOT_PROTOCOL.md says.
- Error lines and codes for each load failure, and an idle loop that answers `state`, `dump` and `reboot` on the maintenance port.
- A test second stage, host tests, QEMU target tests on four boards and CI through RobCo-Core's reusable workflow.

### Fixed

- The BIOS now finds its boot volume on cards with more than one partition. It tries the FAT entries of the MBR in slot order and mounts the first one that holds `/ROBCO/MFBOOT.SYS` or `MFBOOT.BAK`. Before, it mounted the first entry of any type, so a card with a non-FAT partition first did not boot. The `mount` marker and the POST row name the slot.
- A `delay_ms` with ten or more digits now clamps to 10000. It was rejected as bad, and the default of 1000 applied.
- With `delay_ms = 0` the BIOS now prints `key_window_end key=0` after `key_window ms=0`, so the window always has a closing marker.
- A stopped system timer now stops the boot with `E17 TIMER`. The timer probe runs second, after memory. After a failed memory test or a stopped timer, no later probe runs: each shows `SKIPPED`, and no step waits on the timer. Before, the BIOS went on to the audio probe, which waits on the timer and could hang.
