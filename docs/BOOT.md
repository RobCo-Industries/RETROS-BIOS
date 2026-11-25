# Boot flow

RobCo-Core's [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/25f36d82972858554274b6799c00d0259522d56b/docs/spec/BOOT_PROTOCOL.md) is the contract: where the next stage lives, the CPU state and registers at the jump, the boot info, the trust rule and the marker format. This page says what the BIOS does, step by step, and which markers it prints. Every marker starts with `@RC stage=bios` and goes to the PL011 only.

## Steps

| Step | What happens | Markers |
|---|---|---|
| 1. Hardware | `rimg_self_check()`, first in `robco_main()`. PL011 console at 115200 baud. Board detection and the mailbox, through `board_get()`. Framebuffer at 640x480x32 and the text console in green. IRQs and a 100 Hz tick. The input backends: the console and the maintenance port. | `boot`, `init`, `selfcheck`, `input` |
| 2. Banner | The title with the version, two lore lines, the ARM memory from the mailbox and the board, from RobCo-Core's `board_model_name()`. A failed self check adds a line. On the screen and the console. | none |
| 3. Self test | Eight probes, a table on the screen, then a summary. See below. A failed memory test, a stopped timer, or a missing or unreadable card stops the boot. | `post` per probe, `mount`, `post_done` |
| 4. Settings | `/ROBCO/BOOT.CFG`, section `[bios]`. A missing or bad file means defaults. A new colour scheme redraws the screen. | `config`, `settings` |
| 5. Beep | Three tones, 800, 400 and 600 Hz, through `audio_play()`. Skipped when the setting is off or the audio probe did not pass. It plays during the key window. | `beep`, and `audio` from the library's trace backend |
| 6. Key window | `delay_ms` long, 1 second by default. A key on the console or on the maintenance port, or the `shell` command on the maintenance port, ends the window and opens the emergency shell. The boot waits there until `CONTINUE`, `BOOT` or `REBOOT`. With `delay_ms = 0` the screen shows no prompt and keys do not count. The BIOS serves the script lines that came during the self test, prints `key_window ms=0`, then `key_window_end`. A `shell` command among them opens the shell. | `key_window`, `shell_request`, `key_window_end`, `shell` |
| 7. Load | After a failed load the emergency shell opens. See [After a failure](#after-a-failure). `loader_load_fallback()` reads `/ROBCO/MFBOOT.SYS`, or `/ROBCO/MFBOOT.BAK` when the first one fails, into the slot at `0x00200000`. It expects the `BOOT` magic and ABI major 2. | `error`, `fallback`, `load` |
| 8. Handoff | Unmounts the card, waits for the beep to end, copies the ATAGS or DTB to `0x00101000`, builds the boot info at `0x00100000` and enters the next stage with `robco_handoff()`. | `bootinfo`, `jump` |

Settings are read before the beep, one step earlier than in the task list, so that `beep = off` can take effect.

## Self test

The probes run in this order. Memory comes first because every later probe and step needs it. Video comes second: the framebuffer and the mailbox do not use the timer, so the screen shows its real state even when the timer is stopped. The timer comes third, before every probe that waits on it.

| Probe | Passes when | `absent` when | Stops the boot |
|---|---|---|---|
| `memory` | The test of 256 KiB at `0x00400000` finds no fault | Never | Yes, and no later probe runs |
| `video` | `fb_init()` and `console_init()` succeeded | Never | No |
| `timer` | The system timer changes within a million reads | Never | Yes, and no later probe runs |
| `sd` | `sd_init()` succeeded and block 0 reads | No card answers | Yes, also when absent |
| `fat` | A FAT volume mounts. See [Boot volume](#boot-volume). | The card is not usable | Yes, also when absent |
| `rng` | `rng_init()` and one `rng_u32()` succeed | No peripheral base | No |
| `rtc` | A DS1307 or DS3231 answers and its time reads | No chip answers at `0x68` | No |
| `audio` | `audio_init()` returns 0, on the PWM or the trace backend | `audio_init()` returns `AUDIO_ERR_NO_PWM` or `AUDIO_ERR_PIN` | No |

A probe has one of four results: `ok`, `fail`, `absent` or `skipped`. `skipped` means the probe never ran. When the timer does not advance, every later probe is `skipped`, and its `post` marker carries `after=timer`. When the memory test fails, every later probe but the timer is `skipped` with `after=memory`. The timer probe still runs and its row shows its own result: the shell may open after a failed memory test, and the shell waits on the timer. The summary line then ends with `, <n> SKIPPED`.

The library waits on the timer in many places, with deadlines that use the same timer: `sd_init()` and block reads, `timer_delay_us()`, the I2C bus of the RTC, `audio_init()`, the beep, the key window and `input_wait()`. With a stopped timer each of them could hang. So after a stopped timer the BIOS runs none of them. It shows the table, the error line and `BOOT HALTED`, and its idle loop polls the maintenance port without a wait. `uart_init()` also waits on the timer, to set the pins of the PL011, and it runs in step 1 before the self test. A timer that is stopped from power-on hangs there, before any output. The timer probe catches a timer that stops after step 1. The memory test runs first and only reads the timer to report its time in `us=`.

## Boot volume

The `fat` probe calls RobCo-Core's `fs_mount_boot()` with the markers `/ROBCO/MFBOOT.SYS` and `/ROBCO/MFBOOT.BAK`. It runs the boot volume rule of BOOT_PROTOCOL.md. It tries the FAT entries in slot order, then the other entries in slot order:

| Card | Volume the BIOS mounts |
|---|---|
| No partition table, a FAT boot sector at block 0 | The whole card. `boot_part_lba` is 0. |
| Entries that hold `/ROBCO/MFBOOT.SYS` or `/ROBCO/MFBOOT.BAK` | The first of these, FAT entries first |
| Entries that mount, none with those files | The first that mounts, FAT entries first. The load then fails with `E01 NOFILE`. |
| FAT entries, none mounts | None. `fat` fails with `what=mount`. |
| No FAT entry, and no other entry mounts | None. `fat` fails with `what=nofat`, or `what=gpt` on a GPT card. |
| No table and no FAT boot sector | None. `fat` fails with `what=part`. |

A volume holds a boot file only when `MFBOOT.SYS` or `MFBOOT.BAK` is a regular file of at least one byte: a folder of that name or an empty file does not count, so the probe moves on to the next entry, and when no entry qualifies the load reports it on the first volume that mounts (`E01 NOFILE` for a folder, `E02 IO` for an empty file, as the loader rejects a file shorter than the image header).

FAT entries have type `0x01`, `0x04`, `0x06`, `0x0B`, `0x0C` or `0x0E`. A FAT volume behind another type byte is found in the second pass. Extended and GPT protective entries are never mounted. GPT cards are not supported. The POST row and the `mount` marker name the slot. `boot_part_lba` is the start of the chosen volume.

`src/bootvol.c` turns each `fs_mount_boot()` error into the `fat` row and its `what`. `rc=` is the library's last mount or scan error.

| `PART_BOOT_*` | POST row | `what` |
|---|---|---|
| `ERR_DEVICE` | `no partition` | `part` |
| `ERR_NOFAT`, no entry at all | `no partition` | `part` |
| `ERR_NOFAT` | `no FAT partition` | `nofat` |
| `ERR_GPT` | `GPT card, not supported` | `gpt` |
| `ERR_MOUNT` | `no FAT volume mounts` | `mount` |
| `ERR_RELEASE` | `volume does not unmount, error <n>` | `held` |
| `ERR_ARG`, `ERR_BUSY` | `boot volume error <name>` | the library's name |

Each of them is `E02 IO` with `what=fat` at the load. After `ERR_RELEASE` the volume stays mounted as `sd`, and the BIOS keeps it recorded as mounted, as for an old volume below. The BIOS releases the old volume first, so `ERR_BUSY` does not happen.

The shell's `POST` runs the probes again. The `fat` probe first unmounts the volume of the earlier run. RobCo-Core's `fs_unmount()` syncs the card first and keeps the volume when the sync fails, and it has no forced unmount. So while no card answers, the old volume stays registered under `sd`: `fat` fails with `what=held rc=<n>`, mounts nothing, and the BIOS keeps it recorded as mounted. Once a card answers again, the next `POST` unmounts it and mounts the new card. `BOOT` needs a mounted volume and a `fat` row that passed.

One function, `post_record()` in `src/main.c`, stores what a POST run found, at boot and for the shell's `POST`: the table, the fatal result, the stopped-timer flag and, from the `fat` probe, the boot volume and the mount state. A failed run also sets the last error; a passing run keeps it. The settings stay as read at boot: `BOOT.CFG` is not read again.

The Pi firmware loads `RETROS.IMG` from a FAT partition before the BIOS runs. The BIOS does not depend on which one.

## Self check

`rimg_self_check()` checks the running image against the CRC sealed in its header. It runs first in `robco_main()`, because a variable the image writes before the check changes the result. `selfcheck` prints the result after `init`. A failed check adds `BIOS self check FAILED: <name>. Booting anyway.` under the board line, and the boot goes on. `INFO` shows `Self check: passed` or `Self check: FAILED, <name>`.

## Memory test

The memory test window is the start of the OS slot. Nothing lives there before MFBootAgent loads an OS. It is outside the BIOS image and stacks, the handoff area and the MFBootAgent slot. The test writes the data bus bit by bit, checks every address line, then writes and reads two full patterns. That is about 4 accesses per word, 262144 accesses for 64Ki words.

Time on a Pi Zero, a rough count with no measurement: the BIOS runs with the caches off, so every access goes to SDRAM and every instruction is fetched from it. With about 5 instructions per access and 100 to 150 ns per uncached fetch or access, 262144 accesses take 160 to 240 ms. QEMU prints the measured time in `us=`, but QEMU time says nothing about hardware.

## Markers

The events the spec names: `boot`, `mount`, `error`, `fallback`, `load`, `jump` and `shell`. The other events are the BIOS's own. Readers ignore events they do not know.

| Event | Keys | Meaning |
|---|---|---|
| `boot` | `midr soc base board_rev mem entry_mode version` | First marker |
| `init` | `fb console tick hz` | Return codes of step 1 |
| `selfcheck` | `rc size` | Result of `rimg_self_check()`: `ok`, or the `rimg_err_t` name such as `crc`. Information only: the boot goes on. |
| `input` | `serial mini script` | Return codes of the input backends |
| `fault_hook` | `name` | Test images only: the build has a fault hook. The shipped image never prints it. |
| `post` | `name result`, then probe details | One probe. `result` is `ok`, `fail`, `absent` or `skipped`. A skipped probe has `after`, the probe that stopped the test. |
| `mount` | `part lba blocks` | The boot volume is mounted. `part` is the MBR slot, 0 to 3, or `whole` for a card with no partition table. |
| `post_done` | `result ok fail absent skipped us` | The self test summary. `result=fail` when it stops the boot. |
| `config` | `result path`, then `applied clamped bad unknown` or `why` | `result` is `ok`, `missing` or `bad` |
| `settings` | `delay_ms beep scheme` | The settings in use |
| `beep` | `state` | `played`, `off`, `skipped` or `failed` |
| `key_window` | `ms` | The window starts |
| `shell_request` | `source` | A key came, from `console` or `maint`, or `shell` came on the maintenance port (`maint`) |
| `key_window_end` | `key` | The window ended, `key=1` when a key came |
| `error` | `code`, then `path` for a load error and `what` for `io` | One failure. See the codes below. |
| `fallback` | `path from to reason` | The loader moves to `MFBOOT.BAK` |
| `load` | `path size crc addr name` | An image passed every check |
| `bootinfo` | `flags caps dev part_lba fdt_size` | The boot info as handed over |
| `jump` | `entry` | Last line before the next stage |
| `shell` | `reason available` | The emergency shell opens, `available=1`, or cannot run, `available=0`. `reason` is `key` (the key window), `maint` (`shell` on the maintenance port) or the error code that stopped the boot. `available=0` comes when the timer is stopped: with `reason=timer`, or with `reason=memtest` when the memory test failed too. |
| `shell_cmd` | `name rc` | A shell line ended. `name` is the command in lower case, or `unknown`. `rc` is `0`, or why it failed: `usage`, `unknown`, `nofile`, `isdir`, `notdir`, `novol`, `io`, `outside`, `device`, `abort`, `refused`, `fail`, or the load error code for `BOOT`. |
| `diag` | `name result`, then the test's numbers | One diagnostic. `result` is `ok`, `fail`, `absent` or `skipped`. See below. |
| `diag_done` | `ok fail absent skipped` | `DIAG ALL` ended |
| `video_pattern` | `width height` | `DIAG VIDEO` drew its pattern |
| `idle` | | The BIOS waits in its idle loop: after a stopped timer only |
| `reboot` | | The maintenance port or `REBOOT` in the shell asked for a reboot |

The `diag` keys per test:

| `name` | Keys |
|---|---|
| `mem` | `base size us`; on a fault `pattern addr expected actual`; when skipped `reason=window` or `reason=key` |
| `sd` | `lba blocks errors us kib_s` |
| `video` | `width height key` |
| `audio` | `backend notes rc pcm` |
| `timer` | `ppm ticks periods us period_us missed`; `reason=tick hz seen` when the tick does not run or stops, `reason=callback` when no tick callback slot is free |
| `rng` | `words repeats ones bits`; `rc` on a read error |
| `rtc` | `time chip`; `rc` on a read error |
| `input` | `events quit` |

### Sequences

Each marker that opens a step has a closing marker on every path:

- `post` comes once per probe, in table order, eight times on every path. A probe that did not run says `result=skipped`. `post_done` always follows the last one, also when a probe stops the boot.
- `key_window` is always followed by `key_window_end`, also when `delay_ms = 0`. `shell_request` comes between them when a key came.
- `beep` is one marker with its final state. The library's `audio` markers of a played beep end before `jump`, or before `shell` when the load fails: the BIOS waits for the sound to end on both paths.
- An `error` for `MFBOOT.SYS` is followed by `fallback`. Then comes `load` for `MFBOOT.BAK`, or a second `error`.
- Every stop ends with `shell`. A stop in the self test prints `post_done`, `error` and `shell`, and no `config` or later marker. Only a stopped timer adds `idle` after `shell available=0`.
- In the shell, every line that is not blank ends with one `shell_cmd`, except a `BOOT` that starts a second stage: it ends with `jump`, and `REBOOT`, whose `shell_cmd` comes before `reboot`. Markers a command prints, such as `post`, `diag`, `error` or `load`, come before it.

## Error codes

Codes 1 to 8 are the loader's `rimg_err_t` values, with the names from IMAGE_FORMAT.md. Codes 16 and 17 are the BIOS's own and have no path. The screen shows `ERROR E<nn> <NAME>: <path>: <text>`, or `ERROR E<nn> <NAME>: <text>` with no path.

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
| `E17` | `timer` | The system timer does not advance |

When `MFBOOT.SYS` fails, the BIOS prints `error` for it, then `fallback`, and tries `MFBOOT.BAK`. When that fails too, it prints a second `error`.

## After a failure

The BIOS shows the error lines, then opens the emergency shell: `shell reason=<code> available=1`. After `E16 MEMTEST` with a timer that passed, it first shows `WARNING: memory test failed. Results may be wrong.`: the BIOS runs, so the shell can too. It never jumps and never reboots by itself. In the shell, `CONTINUE` is refused, and `BOOT` tries the load again.

A stopped timer is the exception. `input_wait()`, the SD card, the audio and most diagnostics wait on the timer. The line editor and the screen do not, but a shell where most commands hang is no help. So the BIOS shows `BOOT HALTED. Maintenance port is listening.`, prints `shell reason=timer available=0` and `idle`, and its idle loop polls the maintenance port without a wait. A failed memory test with a stopped timer halts the same way. The BIOS shows only `E16 MEMTEST`, the first error, and prints `shell reason=memtest available=0`. The shell opens only on a timer that passed its probe: `post_timer_bad()` in `src/post.c` counts a timer that did not run as bad.

The shell's `POST` follows the same rule. When it finds the timer stopped, it shows `E17 TIMER`, closes the shell and halts as above. When it finds a failed memory test and the timer passed, it shows `E16 MEMTEST` and the warning, and the shell stays open. When it finds both, it shows `E16 MEMTEST`, closes the shell and halts. `post_stop()` in `src/main.c` holds this rule for the boot and the shell. After that `POST`, `CONTINUE` follows the boot's rule: the normal boot goes on only when `post_fatal_probe()` in `src/post.c` finds no failed memory, timer, SD or FAT probe. Otherwise `CONTINUE` names the probe and the error, prints `shell_cmd name=continue rc=<code>`, and the shell stays open. `BOOT` is the operator's choice and stays allowed, as after a boot error. After a failed memory test it shows the warning first.

## The emergency shell

[USER_MANUAL.md](USER_MANUAL.md#the-emergency-shell) describes every command for a person. This section covers what tests and scripts rely on.

- It opens from the key window (`reason=key`), from `shell` on the maintenance port (`reason=maint`), or after a failed boot (`reason=<code>`).
- It reads lines from the PL011 console only, through RobCo-Core's console input backend: CHAR events and the SELECT, BACK, UP and DOWN keys. Script-channel key events are ignored. RobCo-Core has no line editor, so `src/shell.c` has a small one: printable characters, backspace, and a history of four lines on UP and DOWN. A line holds `SH_LINE_MAX` characters, 71: the prompt `RETROS> ` and a full line leave the cursor in column 80, so a line never wraps. A character past the limit gets a bell on the PL011 and is dropped.
- While it waits for a line it runs `input_wait()`, so the maintenance port keeps answering.
- `src/shcmd.c` parses each line. It is pure and host-tested.
- Output goes to the screen and the PL011, through a pager that stops every screenful (29 lines on the 30-row screen, 23 on serial only). `paging off` on the maintenance port turns it off. Tests turn it off.
- When the prompt waits on the PL011, a marker printed then starts on a new line.
- `BOOT` runs the same load and handoff code as the boot, with the same markers. `BOOT <path>` calls `loader_load()` on that file alone, so no `fallback` marker can come.

## Maintenance port

The maintenance port answers in every phase:

| Command | Answer |
|---|---|
| `state` | `phase=<init\|post\|keys\|load\|halted\|shell> error=<code\|none> shell_requested=<0\|1> post=<ok\|fail>`. `error` is the last error: the boot, a `BOOT` or a `POST` that failed. `post` is the last POST run. |
| `dump` | The screen text, as `console_dump()` writes it |
| `shell` | Asks for the shell at the end of the key window. `err shell is open`, `err past the key window`, or `err timer stopped, no shell` otherwise. |
| `paging on`, `paging off` | The shell's pager, on by default |
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
