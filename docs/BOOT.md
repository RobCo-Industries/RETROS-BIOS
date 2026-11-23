# Boot flow

RobCo-Core's [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/BOOT_PROTOCOL.md) is the contract: where the next stage lives, the CPU state and registers at the jump, the boot info, the trust rule and the marker format. This page says what the BIOS does, step by step, and which markers it prints. Every marker starts with `@RC stage=bios` and goes to the PL011 only.

## Steps

| Step | What happens | Markers |
|---|---|---|
| 1. Hardware | PL011 console at 115200 baud. Board detection and the mailbox, through `board_get()`. Framebuffer at 640x480x32 and the text console in green. IRQs and a 100 Hz tick. The input backends: the console and the maintenance port. | `boot`, `init`, `input` |
| 2. Banner | The title with the version, two lore lines, the ARM memory from the mailbox and the board. On the screen and the console. | none |
| 3. Self test | Eight probes, a table on the screen, then a summary. See below. A failed memory test or a missing or unreadable card stops the boot. | `post` per probe, `mount`, `post_done` |
| 4. Settings | `/ROBCO/BOOT.CFG`, section `[bios]`. A missing or bad file means defaults. A new colour scheme redraws the screen. | `config`, `settings` |
| 5. Beep | Three tones, 800, 400 and 600 Hz, through `audio_play()`. Skipped when the setting is off or the audio probe did not pass. It plays during the key window. | `beep`, and `audio` from the library's trace backend |
| 6. Key window | `delay_ms` long, 1 second by default. A key on the console or on the maintenance port sets the shell request, prints "Emergency shell is not built yet." and ends the window. The boot goes on. With `delay_ms = 0` the screen shows no prompt and the BIOS prints `key_window ms=0`, then `key_window_end key=0` at once. | `key_window`, `shell_request`, `key_window_end` |
| 7. Load | `loader_load_fallback()` reads `/ROBCO/MFBOOT.SYS`, or `/ROBCO/MFBOOT.BAK` when the first one fails, into the slot at `0x00200000`. It expects the `BOOT` magic and ABI major 2. | `error`, `fallback`, `load` |
| 8. Handoff | Unmounts the card, waits for the beep to end, copies the ATAGS or DTB to `0x00101000`, builds the boot info at `0x00100000` and enters the next stage with `robco_handoff()`. | `bootinfo`, `jump` |

Settings are read before the beep, one step earlier than in the task list, so that `beep = off` can take effect.

## Self test

| Probe | Passes when | `absent` when | Stops the boot |
|---|---|---|---|
| `memory` | The test of 256 KiB at `0x00400000` finds no fault | Never | Yes |
| `video` | `fb_init()` and `console_init()` succeeded | Never | No |
| `sd` | `sd_init()` succeeded and block 0 reads | No card answers | Yes, also when absent |
| `fat` | A partition is found and FatFs mounts it | The card is not usable | Yes, also when absent |
| `timer` | The system timer changes within a million reads | Never | No |
| `rng` | `rng_init()` and one `rng_u32()` succeed | No peripheral base | No |
| `rtc` | A DS1307 or DS3231 answers and its time reads | No chip answers at `0x68` | No |
| `audio` | `audio_init()` returns 0, on the PWM or the trace backend | `audio_init()` returns `AUDIO_ERR_NO_PWM` or `AUDIO_ERR_PIN` | No |

The memory test window is the start of the OS slot. Nothing lives there before MFBootAgent loads an OS. It is outside the BIOS image and stacks, the handoff area and the MFBootAgent slot. The test writes the data bus bit by bit, checks every address line, then writes and reads two full patterns. That is about 4 accesses per word, 262144 accesses for 64Ki words.

Time on a Pi Zero, a rough count with no measurement: the BIOS runs with the caches off, so every access goes to SDRAM and every instruction is fetched from it. With about 5 instructions per access and 100 to 150 ns per uncached fetch or access, 262144 accesses take 160 to 240 ms. QEMU prints the measured time in `us=`, but QEMU time says nothing about hardware.

## Markers

The events the spec names: `boot`, `mount`, `error`, `fallback`, `load`, `jump` and `shell`. The other events are the BIOS's own. Readers ignore events they do not know.

| Event | Keys | Meaning |
|---|---|---|
| `boot` | `midr soc base board_rev mem entry_mode version` | First marker |
| `init` | `fb console tick hz` | Return codes of step 1 |
| `input` | `serial mini script` | Return codes of the input backends |
| `post` | `name result`, then probe details | One probe. `result` is `ok`, `fail` or `absent`. |
| `mount` | `lba blocks` | The FAT partition is mounted |
| `post_done` | `result ok fail absent us` | The self test summary. `result=fail` when it stops the boot. |
| `config` | `result path`, then `applied clamped bad unknown` or `why` | `result` is `ok`, `missing` or `bad` |
| `settings` | `delay_ms beep scheme` | The settings in use |
| `beep` | `state` | `played`, `off`, `skipped` or `failed` |
| `key_window` | `ms` | The window starts |
| `shell_request` | `source` | A key came, from `console` or `maint` |
| `key_window_end` | `key` | The window ended, `key=1` when a key came |
| `error` | `code path`, `what` for `io` | One failure. See the codes below. |
| `fallback` | `path from to reason` | The loader moves to `MFBOOT.BAK` |
| `load` | `path size crc addr name` | An image passed every check |
| `bootinfo` | `flags caps dev part_lba fdt_size` | The boot info as handed over |
| `jump` | `entry` | Last line before the next stage |
| `shell` | `reason available=0` | The boot stopped. The emergency shell is not built yet, so `available=0`. |
| `idle` | | The BIOS waits in its idle loop |
| `reboot` | | The maintenance port asked for a reboot |

### Sequences

Each marker that opens a step has a closing marker on every path:

- `post` comes once per probe, in table order, eight times on every path. `post_done` always follows the last one, also when a probe stops the boot.
- `key_window` is always followed by `key_window_end`, also when `delay_ms = 0`. `shell_request` comes between them when a key came.
- `beep` is one marker with its final state. The library's `audio` markers of a played beep end before `jump`, or before `shell` when the load fails: the BIOS waits for the sound to end on both paths.
- An `error` for `MFBOOT.SYS` is followed by `fallback`. Then comes `load` for `MFBOOT.BAK`, or a second `error`.
- Every stop ends with `shell`, then `idle`. A stop in the self test prints `post_done`, `error`, `shell` and `idle`, and no `config` or later marker.

## Error codes

Codes 1 to 8 are the loader's `rimg_err_t` values, with the names from IMAGE_FORMAT.md. The screen shows `ERROR E<nn> <NAME>: <path>: <text>`.

| Screen | `code=` | Cause |
|---|---|---|
| `E01` | `nofile` | The file is not on the card |
| `E02` | `io` | No card, a card that does not read, no FAT partition, or a read error. `what=sd` or `what=fat`. |
| `E03` | `magic` | Not a `BOOT` image |
| `E04` | `version` | Unknown header version |
| `E05` | `size` | `file_size` does not match the file |
| `E06` | `bounds` | The image does not fit the slot at `0x00200000` |
| `E07` | `abi` | ABI major is not 2 |
| `E08` | `crc` | The image CRC does not match |
| `E16` | `memtest` | The memory test found a fault |

When `MFBOOT.SYS` fails, the BIOS prints `error` for it, then `fallback`, and tries `MFBOOT.BAK`. When that fails too, it prints a second `error`.

## After a failure

The BIOS shows the error lines and `BOOT HALTED. Maintenance port is listening.`, prints `shell` and `idle`, and waits. It never jumps and never reboots by itself. The maintenance port answers:

| Command | Answer |
|---|---|
| `state` | `phase=<init\|post\|keys\|load\|halted> error=<code\|none> shell_requested=<0\|1> post=<ok\|fail>` |
| `dump` | The screen text, as `console_dump()` writes it |
| `reboot` | Prints `reboot` and restarts through the watchdog |

The library's built-in script commands, such as `key` and `char`, work too. The maintenance port is the mini UART. In QEMU it is the second serial port. On hardware the mini UART reaches no pin, so the port exists only in QEMU.

## Settings

`/ROBCO/BOOT.CFG` is shared with MFBootAgent. The spec defines `[boot]` and `[entry ...]` for it and no key for the BIOS. The BIOS reads its own `[bios]` section and ignores the others.

| Key | Values | Default |
|---|---|---|
| `delay_ms` | Key window in ms, 0 to 10000. A larger number is clamped to 10000. | `1000` |
| `beep` | `on`, `off`, `yes`, `no`, `true`, `false`, `1`, `0` | `on` |
| `scheme` | `green`, `amber`, `blue`, `white` | `green` |

A `delay_ms` value is one or more ASCII digits and nothing else. Leading zeros are fine. A sign, a space inside the number, a unit such as `ms` or any other character makes the value bad. Any run of digits counts: a value past 4294967295 saturates there, then the clamp to 10000 applies. So `delay_ms = 1000000000` gives 10000, and `delay_ms = 10ms` is bad.

A bad value or an unknown key is skipped and counted in `config`. A line that does not parse makes the whole file count as bad: every key takes its default.

## Boot info

`bootinfo_build()` fills every field the BIOS set up: board, memory, firmware registers, the ATAGS or DTB copy, the PL011, the framebuffer and the console with its cursor and scheme, and the caps bits for the mini UART and the SD card. Then `bios_bootinfo_set_boot()` writes `boot_dev = 1` (SD on EMMC), `boot_part_lba`, the fallback flag and `boot_path`, and reseals the block. `cmdline` stays empty. `boot_flags` bit 1, maintenance mode, is never set.
