# Testing

`make test` runs everything: host tests, then the target tests on four QEMU boards. Every test can fail.

## Host tests

`make test-host` runs, in this order:

1. `make check-pins`: the CI workflow calls RobCo-Core's reusable workflow at the submodule commit with the image tag from `ci/IMAGE_TAG`, and every RobCo-Core link in the Markdown files points at that commit.
2. `tests/host/check_pins_test.sh`: the pin and link checks pass on this tree, fail on a wrong commit, a wrong tag and a stale link, and `check-pins` runs both.
3. `tests/host/sdimage_test.sh`: `make sdimage` puts the expected files on the card, and `STAGE2=` replaces the second stage.
4. `tests/host/test_*.c`, built with `-Werror` and the sanitizers, linked with the pure files:

| Test | Covers |
|---|---|
| `test_bootlines.c` | Every screen line, with numbers at the limits of their types |
| `test_settings.c` | Defaults, every key, clamps, bad values, unknown keys, syntax errors |
| `test_post.c` | The memory test on good memory, a stuck data bit, a stuck bit in one word and a shorted address line. The fatal rules and the counts. |
| `test_shcmd.c` | Every command, every bad form, case, no abbreviations, the 78-character limit, hex and decimal numbers at their limits, the MEM address rules, the hex dump line, the pager count, and the `DIR` listing from a fake folder that ends, overflows or fails partway |
| `test_diagcalc.c` | The default `DIAG MEM` window for each board's memory size and around a framebuffer, the allowed windows, the patterns on good memory and with a stuck bit or a shorted address line, the RNG rule at its edges, the timer ppm with exact inputs, its limit and missed ticks, and the SD speed |
| `test_bootvol.c` | Which MBR entries count as FAT, which one boots, and the release of an earlier volume when the unmount fails |
| `test_boot.c` | Error codes against `rimg_strerror()`, every error line within 79 columns, the boot fields of the boot info |

## Target tests

`make test-target` boots the BIOS in QEMU through RobCo-Core's harness. `BOARD=raspi0` runs one board. The tests build their cards with mtools. Most cards set `delay_ms = 0` to save time.

| File | Covers |
|---|---|
| `test_boot.py` | The happy path: every marker in order, every check of the test stage, the boot text, a golden of the screen after the jump |
| `test_bootvol.py` | Cards with several MBR entries: FAT in slot 1 after a Linux or an empty slot 0, the boot files on the second of two FAT volumes, both volumes with them, and no FAT entry |
| `test_load.py` | The fallback for a corrupt and for a missing primary, each bad image, both files missing, a bad primary and a bad backup, no card, a card with no FAT volume, a golden of the CRC error screen, `reboot` on the maintenance port |
| `test_keys.py` | A key on the console and on the maintenance port opens the shell and `CONTINUE` boots; `shell` on the maintenance port with `delay_ms = 0`; no key |
| `test_shell.py` | Every command and its error paths, a damaged card whose folder and file chains break after the first cluster, `BOOT` with a good image, a bad one and an explicit path with no fallback, `CONTINUE` refused after a failure, a card-less shell, `REBOOT`, the pager with paging on, a golden of the help screen |
| `test_diag.py` | `DIAG ALL` on every board, without and with QEMU's `ds1338` clock chip; `DIAG MEM` on a small window, a refused window, and the stuck bit of the memtest fault image; the `VIDEO` pattern pixels; `INPUT` with typed keys |
| `test_cardswap.py` | Card swaps through QMP: a card put in after a boot without a card, pulled out, and put back; a good card in place of one with no FAT volume. Each step runs `POST`, `VOL`, `DIR`, `INFO` or `BOOT`. |
| `test_settings.py` | `BOOT.CFG` settings take effect, the scheme on screen, the clamp, a bad file, a missing file |
| `test_post_faults.py` | A failed memory test and a stopped timer, through fault images: the later probes are skipped and the error shows. A failed memory test opens the shell with a warning. A stopped timer halts, nothing waits on the timer, the maintenance port answers and refuses `shell`. Also checks that the shipped image has no fault hook. |
| `test_env_hygiene.py` | No test copies the host environment |

The bad images are the test stage with header bytes patched, resealed when the case needs a valid CRC.

## The test second stage

`tests/stage2` stands in for MFBootAgent. It prints one `ev=check` marker per check: the registers at entry, the CPU mode, SCTLR, the boot info CRC, the trust rule, the CRC of its own image as loaded, and the boot info fields. Then `ev=bootinfo` with the values and `ev=checks passed=<n> failed=<n>`.

## Fault images

QEMU cannot stop its timer, break its RAM or lose a tick interrupt from outside. The tests build the BIOS again under `build/fault_timer`, `build/fault_memtest` and `build/fault_tick` with `EXTRA_CFLAGS=-DRETROS_FAULT_TIMER=1`, `-DRETROS_FAULT_MEMTEST=1` or `-DRETROS_FAULT_TICK=1`. The timer hook makes the timer probe read a value that never changes. The memtest hook makes the memory test read bit 7 stuck at 1 in one word. The tick hook makes the `DIAG TIMER` callback lose every tenth tick interrupt, and `test_diag.py` checks that the test fails. Each fault image prints a `fault_hook` marker. `make` never sets these flags, and a test checks that `build/retros.img` has none of the hooks.

## Driving the shell

Tests type each line on the PL011 console, serial port 0, as a person would, through `sh()` in `tests/retros.py`. It waits for the line's `shell_cmd` marker and returns the marker and the output lines. It types in chunks of 16 bytes: RobCo-Core's input queue holds 32 events, and the console backend reads up to 64 bytes per poll, so a longer burst loses bytes. `open_shell()` waits for `shell ... available=1` and sends `paging off` on the maintenance port. Only `test_pager` keeps the pager on.

## Goldens

`tests/golden/boot_<board>.png`, `error_crc_<board>.png` and `shell_help_<board>.png` are screenshots. The help screen is fixed text: `CLS`, then `HELP`. `ROBCO_UPDATE_GOLDEN=1` rewrites them. Look at the new images before you commit them.

A golden must not show text that depends on the toolchain: sizes, CRCs, build dates, compiler versions. CI runs in a container with its own `arm-none-eabi-gcc`, so the compiled test stage in `tests/stage2` has another size and CRC there than on your machine. The boot golden therefore loads a fixed stage instead: `tests/fixed_stage.py` seals three ARM words with `tools/rimg.py` into `build/fixed_stage.img` at test time. `test_fixed_stage_is_pinned` pins its size, CRC and SHA-256, and the golden test checks that the scenario shows no other image size or CRC. The functional tests keep the compiled stage and compute its expected size and CRC from the file they put on the card.

## Timing

`DIAG TIMER` stamps the system timer inside the tick interrupt, at the first tick and 200 ticks later, so only the difference of two interrupt latencies enters the result. It passes with no missed tick and within 5000 ppm, one tick period over the window. `test_diag.py` asserts 200 ticks, 200 periods, no missed tick and less than 5000 ppm. In QEMU on a busy computer the readings spread over about plus or minus 2500 ppm, because the guest gets no CPU time for some milliseconds. A pause over 10 ms shows as a missed tick and fails the test: rerun it alone to tell host load from a fault.

The key window test checks lower bounds only: a window with no key lasts at least `delay_ms` as seen from the host, and a key ends it at least 0.5 s early. Other QEMU runs on the same machine slow the guest down, so no test bounds a duration from above more tightly than that.
