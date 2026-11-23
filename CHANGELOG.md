# Changelog

All notable changes to RETROS-BIOS are listed here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- The emergency shell on the serial console: `HELP`, `INFO`, `POST`, `VOL`, `DIR`, `TYPE`, `MEM`, `BOOT`, `CONTINUE`, `DIAG`, `REBOOT` and `CLS`. It opens from the key window, after a boot error, and from `shell` on the maintenance port. Every command ends with a `shell_cmd` marker. A line holds 71 characters, so it fits one row after the prompt.
- `POST` in the shell finds a card put in after power-on. A pulled card that does not unmount shows as `what=held`, and the next `POST` with a card that answers frees it.
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

- Rebuilt the BIOS on RobCo-Core, a submodule at `external/robco-core`. The prototype is gone.
- A boot error now opens the shell instead of halting. Only a stopped timer still halts, with `shell available=0`.
- A key in the key window opens the shell. The boot goes on with `CONTINUE`.

### Fixed

- The BIOS now finds its boot volume on cards with more than one partition. It tries the FAT entries of the MBR in slot order and mounts the first one that holds `/ROBCO/MFBOOT.SYS` or `MFBOOT.BAK` as a non-empty file. Before, it mounted the first entry of any type, so a card with a non-FAT partition first did not boot. The `mount` marker and the POST row name the slot.
- A `delay_ms` with ten or more digits now clamps to 10000. It was rejected as bad, and the default of 1000 applied.
- With `delay_ms = 0` the BIOS now prints `key_window_end key=0` after `key_window ms=0`, so the window always has a closing marker.
- A stopped system timer now stops the boot with `E17 TIMER`. The timer probe runs third, after memory and video. After a failed memory test or a stopped timer, no later probe runs: each shows `SKIPPED`, and no step waits on the timer. Before, the BIOS went on to the audio probe, which waits on the timer and could hang.
