# Changelog

All notable changes to RETROS-BIOS are listed here.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed

- Rebuilt the BIOS on RobCo-Core, a submodule at `external/robco-core`. The prototype is gone.

### Added

- A power-on self test: memory, video, SD card, FAT volume, timer, RNG, RTC and audio, with a table on screen and a marker per probe.
- A boot screen with the version, the memory size and the board, and two lore lines.
- `[bios]` settings in `/ROBCO/BOOT.CFG`: `delay_ms`, `beep` and `scheme`.
- A power-on beep and a key window. A key only prints that the emergency shell is not built yet.
- Loading `/ROBCO/MFBOOT.SYS` with `/ROBCO/MFBOOT.BAK` as the fallback, and the handoff with the boot info as BOOT_PROTOCOL.md says.
- Error lines and codes for each load failure, and an idle loop that answers `state`, `dump` and `reboot` on the maintenance port.
- A test second stage, host tests, QEMU target tests on four boards and CI through RobCo-Core's reusable workflow.
