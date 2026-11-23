# RETROS-BIOS user manual

This manual is for a person with a Raspberry Pi, an SD card and a serial cable.

Nothing here is validated on real hardware yet. Every behaviour below was seen in QEMU only.

## What RETROS-BIOS is

RETROS-BIOS is the first program of the RobCo terminal chain. The Pi firmware loads it from the SD card. It tests the hardware, shows a boot screen, and loads the second stage, `/ROBCO/MFBOOT.SYS`, from the card. When the boot fails, or when you press a key early, it opens the emergency shell. The shell lets you look at the card, the memory and the hardware, and start a second stage by hand.

## What you need

- A Raspberry Pi 1, Zero, 2 or 3. The Pi 4 is not supported.
- An SD card with an MBR partition table and a FAT partition. GPT cards are not supported.
- A 3.3 V USB serial cable. Never use a 5 V cable: it can damage the Pi.
- A terminal program on your computer, such as `screen`, `minicom` or PuTTY, set to 115200 baud, 8 data bits, no parity, 1 stop bit, no flow control.
- A screen on the HDMI port is optional. All text also goes to the serial line.

## Building a card

On a computer with the build tools from [BUILD.md](BUILD.md):

```
git submodule update --init --recursive
make
make bootdir DIR=/path/to/card
```

`make bootdir` copies the Pi firmware, `config.txt`, `RETROS.IMG`, `ROBCO/BOOT.CFG` and a second stage as `ROBCO/MFBOOT.SYS` to the FAT partition of the card. A `ROBCO/BOOT.CFG` already on the card stays. Without a real second stage, the card holds the test stage, which prints its checks on the serial line and stops.

## Wiring the serial console

The console is the PL011 UART on GPIO 14 and 15. The pin facts are in RobCo-Core's [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/BOARD_SUPPORT.md#uart-policy).

| Pi header pin | Signal | Cable |
|---|---|---|
| 6 | Ground | Ground |
| 8 | GPIO 14, TXD from the Pi | RXD of the cable |
| 10 | GPIO 15, RXD to the Pi | TXD of the cable |

Do not connect the 5 V or 3.3 V wire of the cable. Power the Pi from its own supply.

On a Pi 3, Bluetooth normally owns this UART. The BIOS takes it back itself. Whether this works on a real Pi 3 is not validated yet. BOARD_SUPPORT.md lists what to try if the line stays silent.

## The boot screen

The BIOS writes the same lines on the screen and on the serial line. In order:

| Line | Meaning |
|---|---|
| `RETROS BIOS 0.1.0` | The version |
| `RBIOS-4.02.08.00 52EE5.E7.E8` | Fixed lore text. It means nothing. |
| `Copyright 2201-2203 Robco Ind.` | Fixed lore text |
| `Uppermem: <n> KB` | ARM memory, as the firmware reports it |
| `Board: <model>, <soc>, rev 0x<rev>` | The board, from its revision code |
| `POWER-ON SELF TEST` | The self test starts |
| Eight rows, `MEMORY` to `AUDIO` | One row per probe: `OK`, `FAILED`, `ABSENT` or `SKIPPED`, then a detail |
| `POST: <n> OK, <n> FAILED, <n> ABSENT` | The summary. `, <n> SKIPPED` follows when a probe stopped the test. |
| `Settings: delay <n> ms, beep on, scheme green (<source>)` | The settings in force. The source is `/ROBCO/BOOT.CFG` or `defaults`. |
| `Press any key for the emergency shell` | The key window is open. Not shown with `delay_ms = 0`. |
| `Loading /ROBCO/MFBOOT.SYS` | The load starts |
| `Loaded <name>: <n> bytes, CRC 0x<crc>` | The image passed every check |
| `Starting <name> at 0x<addr>` | The last line before the second stage runs |

The probes:

| Row | `OK` when | `ABSENT` when |
|---|---|---|
| `MEMORY` | 256 KiB at `0x00400000` pass the test | Never |
| `VIDEO` | The screen at 640x480 works | Never |
| `TIMER` | The system timer advances | Never |
| `SD CARD` | The card answers and block 0 reads | No card |
| `FAT` | A FAT volume mounts | No usable card |
| `RNG` | The hardware generator gives a number | No generator |
| `RTC` | A DS1307, DS1338 or DS3231 clock answers at I2C address `0x68` | No clock chip |
| `AUDIO` | The PWM audio or the trace backend starts | No PWM output |

## Settings in BOOT.CFG

`/ROBCO/BOOT.CFG` is a text file shared with the second stage. The BIOS reads only its `[bios]` section:

```
[bios]
delay_ms = 1000
beep = on
scheme = green
```

| Key | Values | Default |
|---|---|---|
| `delay_ms` | The key window in milliseconds, 0 to 10000. Larger values become 10000. | `1000` |
| `beep` | `on`, `off`, `yes`, `no`, `true`, `false`, `1`, `0` | `on` |
| `scheme` | Screen colours: `green`, `amber`, `blue`, `white` | `green` |

A bad value is skipped and the key keeps its default. A line that does not parse makes the BIOS ignore the whole file. [BOOT.md](BOOT.md#settings) has the exact rules.

## Error codes

An error shows as `ERROR E<nn> <NAME>: <path>: <text>`. The shell opens after it, except for `E17`.

| Code | Meaning | What to do |
|---|---|---|
| `E01 NOFILE` | The file is not on the card | Copy `MFBOOT.SYS` to the `ROBCO` folder of the FAT partition. Check the name with `DIR`. |
| `E02 IO` | No card, a card that does not read, or no FAT partition | Reseat the card. Check that it has an MBR and a FAT partition. Run `VOL` and `POST`. |
| `E03 MAGIC` | The file is not a boot image | The file is something else, or damaged. Copy it again. |
| `E04 VERSION` | Unknown image header version | The image is for another BIOS version. Rebuild it. |
| `E05 SIZE` | The size in the header does not match the file | The copy is incomplete. Copy it again. |
| `E06 BOUNDS` | The image does not fit its slot at `0x00200000` | The image is not a second stage. Rebuild it. |
| `E07 ABI` | Wrong ABI version | The second stage is for another BIOS. Rebuild it. |
| `E08 CRC` | The image CRC does not match | The file is damaged. Copy it again. The BIOS tries `MFBOOT.BAK` first. |
| `E16 MEMTEST` | The memory test found a fault | The shell opens with a warning. Run `DIAG MEM`. The board may be faulty. |
| `E17 TIMER` | The system timer does not advance | The BIOS halts. Nothing waits on the timer, so no shell. Power the board off and on. |

When `MFBOOT.SYS` fails, the BIOS tries `MFBOOT.BAK` and shows a second error if that fails too.

## The emergency shell

### Opening it

- Press any key while `Press any key for the emergency shell` shows. The boot waits in the shell. `CONTINUE` goes on with the boot.
- After any boot error except `E17`, the shell opens by itself under the error lines.
- On the maintenance port, a script can send `shell` before the key window ends. The shell then opens at the end of the window, even with `delay_ms = 0`.

The shell shows `RETROS emergency shell. Type HELP for the commands.`, then the prompt `RETROS> `.

### Typing

Type on the serial terminal. A keyboard on the Pi is not supported. One command per line, at most 78 characters. Backspace deletes the last character. Pasting more than about 30 characters at once can lose characters: the input queue holds 32 events. The up and down arrows bring back the last four lines. Left and right do not move the cursor. Names are not case-sensitive. Words are separated by spaces. Addresses and lengths are hex, with or without `0x`. Counts are decimal. There are no abbreviations.

A wrong line gets one line that says what is wrong, ending with `Type HELP.`

Long output stops after each screenful with `-- more (any key, Q to stop) --`. Any key shows the next screenful. `Q` stops the output.

### Commands

`HELP [command]` lists the commands. With a name, it shows that command's usage.

```
RETROS> HELP MEM
MEM <addr> [len]
  Hex dump of RAM. addr and len are hex, with or without 0x.
  len is 1 to 1000 (hex), default 40. Device addresses are refused.
```

`INFO` shows the version, the board, the memory, the boot volume, the settings, the last error and the time since power-on. The settings are the ones read at boot: `POST` does not read `BOOT.CFG` again. The last error is the most recent one, from the boot, a `BOOT` or a `POST` that failed. A `POST` that passes does not clear it.

```
RETROS> INFO
RETROS BIOS 0.1.0
Board: Raspberry Pi 2 Model B, bcm2836, rev 0x00a21041
Memory: 983040 KB ARM RAM at 0x00000000, peripherals at 0x3f000000
Boot volume: part 0, LBA 2048, 30720 blocks
Settings: delay 0 ms, beep on, scheme green (/ROBCO/BOOT.CFG)
Last error: E01 NOFILE: file not found
Uptime: 0 h 00 min 00 s
```

`POST` runs the self test again and shows its table. It unmounts the card and mounts it again, so a card put in after power-on can be found. To swap cards, take the old card out, put the new one in, then run `POST`, `VOL` and `DIR` to check it, and `BOOT` to start from it.

A pulled card stays mounted until a card answers again: the unmount checks the card first and fails while none answers. `POST` then shows `FAT FAILED old volume still mounted, error <n>`, `INFO` adds the line `Boot volume card not answering. Put a card in and run POST.`, and `BOOT` stops with `E02 IO`. Put a card in and run `POST` again: the old volume goes and the new card mounts.

`VOL` shows the partition table: slot, type byte, start, size in blocks, FAT or not. `*` marks the boot volume.

```
RETROS> VOL
SLOT  TYPE  START LBA     BLOCKS  FAT  BOOT
   0  0x0e       2048      30720  yes  *
* is the boot volume.
```

`DIR [path]` lists a folder of the boot volume, sorted by name, any case. The default is `/ROBCO`. A folder shows `<DIR>`. The last line counts files, folders and bytes. A folder with more than 128 entries shows the first 128 it reads, sorted, and a line with the number left out. When a read fails partway, for example on a damaged card, `DIR` shows the entries it read, then `Read error after <n> entries: error <code>`, with no totals.

```
RETROS> DIR
BOOT.CFG                                                             20
1 files, 0 folders, 20 bytes
```

`TYPE <path>` prints a text file. It shows at most 16 KiB. A byte that is not printable shows as `.`. A longer file ends with a line that gives its size. A read error shows `Read error: <path>: error <code>` and none of the file.

```
RETROS> TYPE /ROBCO/BOOT.CFG
[bios]
delay_ms = 0
```

`MEM <addr> [len]` shows memory in hex and ASCII, 16 bytes per line. `len` is hex, 1 to `1000` (4096 bytes), default `40` (64 bytes). Only ARM RAM is allowed. A peripheral address is refused, because reading some registers changes the device. There is no write command. Each word is read with an abort-safe read: a byte that cannot be read shows as `--`.

```
RETROS> MEM 8020 10
00008020: 42 4f 4f 54 00 00 00 00 01 00 40 00 00 00 00 00  BOOT......@.....
```

`BOOT [path]` loads and starts a second stage. Without a path it loads `/ROBCO/MFBOOT.SYS`, then `/ROBCO/MFBOOT.BAK` if that fails. With a path it loads exactly that file, with no fallback. The checks are the same as at power-on. On a failure the error line shows and the prompt comes back.

```
RETROS> BOOT /ROBCO/MFBOOT.BAK
Loading /ROBCO/MFBOOT.BAK
```

`CONTINUE` goes on with the normal boot. It works only when a key opened the shell. After a boot error it says there is nothing to continue.

`DIAG [test]` runs the diagnostics. See below.

`REBOOT` restarts the board through the watchdog.

`CLS` clears the screen.

## Diagnostics

`DIAG` alone lists the tests. `DIAG <test>` runs one. `DIAG ALL` runs all but `INPUT` and shows a table like the self test, then a summary line:

```
RETROS> DIAG ALL
DIAGNOSTICS
  MEM      OK      16 MiB at 0x00400000, 4 patterns
  SD       OK      256 blocks at LBA 2048, 3531 KiB/s
  VIDEO    OK      640x480 pattern shown
  AUDIO    OK      trace, 8 notes, PCM played
  TIMER    OK      -114 ppm over 99 ticks, 0 missed
  RNG      OK      0 repeats, 16489 of 32768 bits set
  RTC      ABSENT  no clock chip
DIAG: 6 OK, 0 FAILED, 1 ABSENT, 0 SKIPPED
```

The examples in this manual came from QEMU on `raspi2b`, with a test card. Real hardware gives other numbers.

| Test | What it does | `OK` when |
|---|---|---|
| `MEM [addr len]` | Writes and reads RAM with four patterns: walking ones in the first word of every 4 KiB, each word's own address, a checkerboard, and its complement. Each pattern is written over the whole window, then read back. | No word differs. A fault shows the pattern, the address, what was read and what was expected. |
| `SD [count]` | Reads `count` blocks, 256 by default, from the start of the boot volume, or from block 0 when no volume is mounted. It never writes. | No read error. It shows the speed in KiB/s. |
| `VIDEO` | Shows colour bars, a grid every 40 pixels and a white border one pixel inside the edge, for 3 seconds or until a key. Then the text comes back. | The screen works. You judge the pattern. |
| `AUDIO` | Plays a rising scale of eight notes, then a short PCM sound. It names the backend: `pwm` or `trace`. | The scale starts. `PCM not supported` means only the PCM part failed. |
| `TIMER` | Counts tick interrupts over one second of the system timer. | The tick agrees with the timer within 5000 parts per million. It also shows missed ticks. |
| `RNG` | Reads 1024 words from the hardware generator. | No word equals the one before, and the count of set bits is within 512 of half. This is a sanity check, not a test of randomness. |
| `RTC` | Reads the clock chip and shows the time in UTC. | The chip answers with a valid time. |
| `INPUT` | Shows each key from the serial terminal as it arrives, for 10 seconds or until `Q`. | Always. You judge the codes. |

`ABSENT` means the hardware is not there: no card, no screen, no audio, no generator, no clock chip. `SKIPPED` means the test did not run, for example a `MEM` window that is not allowed, or a key that stopped `MEM`.

### The memory window

`DIAG MEM` with no window tests from `0x00400000` to the end of ARM RAM, in whole MiB. Below `0x00400000` are the BIOS, its stacks, the handoff area and the second-stage slot, so the test never writes there. The framebuffer is GPU memory, above ARM RAM. If it ever lies inside the window, the test takes the larger part on either side of it. On a Pi 2 with 948 MiB of ARM RAM the window is 944 MiB. The test shows progress on the screen every MiB. Any key stops it.

`DIAG ALL` tests only the first 16 MiB of that window, so the whole table takes seconds, not minutes.

A window you give must start at or above `0x00400000`, end inside ARM RAM, and use multiples of 4 for the address and the length.

## The maintenance port

The maintenance port is a line-based script channel on the mini UART. On a real Pi the mini UART reaches no pin, so the port exists only in QEMU, as the second serial port. Scripts and tests use it. Each line gets `ok` or `err <reason>`.

| Command | Answer |
|---|---|
| `state` | `phase=<init\|post\|keys\|load\|halted\|shell> error=<code\|none> shell_requested=<0\|1> post=<ok\|fail>`. `error` is the last error, as in `INFO`. `post` is the result of the last POST run, at boot or from the shell. |
| `dump` | The screen text |
| `shell` | Asks for the shell at the end of the key window. Refused once the shell is open, after the key window, and after a stopped timer. |
| `paging off`, `paging on` | Turns the shell's pager off or on. On by default. |
| `reboot` | Restarts the board |

RobCo-Core's built-in commands, such as `key` and `char`, work too. The shell reads its lines from the serial console only. To drive the shell, a script writes the command line to the console, as a person types it.

Every shell command ends with a marker line on the serial console, `@RC stage=bios ev=shell_cmd name=<command> rc=<0|reason>`. [BOOT.md](BOOT.md#markers) lists every marker.

## Troubleshooting

| Symptom | Likely cause | What to try |
|---|---|---|
| Nothing on the serial line | Wrong wiring, wrong baud rate, or a Pi 3 whose UART stays with Bluetooth | Swap TXD and RXD. Set 115200 8N1. On a Pi 3, see BOARD_SUPPORT.md. |
| Garbage on the serial line | Wrong baud rate | Set 115200 baud. |
| Text on serial, nothing on the screen | No HDMI signal at 640x480 | Try another screen. The `VIDEO` row of the self test says `FAILED` when the framebuffer did not start. |
| Typing in the shell does nothing | The terminal sends nothing, or the TXD wire is loose | Check pin 10. Turn local echo off: the shell echoes. |
| `E02 IO` with a card in | No MBR, no FAT partition, or a GPT card | Run `VOL`. Repartition the card with an MBR and a FAT partition. |
| `E01 NOFILE` | `MFBOOT.SYS` is missing or in the wrong folder | Run `DIR /ROBCO`. Copy the file there. |
| `E08 CRC` | The file is damaged | Copy it again. Run `DIAG SD` to check the card. |
| `E16 MEMTEST` | A RAM fault | Run `DIAG MEM`. Try another board. |
| The board halts with `E17 TIMER` | The system timer stopped | Power off and on. A timer that never starts stops the BIOS before any text. |
| `DIAG TIMER` fails | The tick interrupt is late or lost | Run it again. A repeat failure points at the board or at long masked sections. |
