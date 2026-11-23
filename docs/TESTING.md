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
| `test_boot.c` | Error codes against `rimg_strerror()`, every error line within 79 columns, the boot fields of the boot info |

## Target tests

`make test-target` boots the BIOS in QEMU through RobCo-Core's harness. `BOARD=raspi0` runs one board. The tests build their cards with mtools. Most cards set `delay_ms = 0` to save time.

| File | Covers |
|---|---|
| `test_boot.py` | The happy path: every marker in order, every check of the test stage, the boot text, a golden of the screen after the jump |
| `test_load.py` | The fallback for a corrupt and for a missing primary, each bad image, both files missing, a bad primary and a bad backup, no card, a card with no FAT volume, a golden of the CRC error screen, `reboot` on the maintenance port |
| `test_keys.py` | A key on the console, a key on the maintenance port, no key |
| `test_settings.py` | `BOOT.CFG` settings take effect, the scheme on screen, the clamp, a bad file, a missing file |
| `test_env_hygiene.py` | No test copies the host environment |

The bad images are the test stage with header bytes patched, resealed when the case needs a valid CRC.

## The test second stage

`tests/stage2` stands in for MFBootAgent. It prints one `ev=check` marker per check: the registers at entry, the CPU mode, SCTLR, the boot info CRC, the trust rule, the CRC of its own image as loaded, and the boot info fields. Then `ev=bootinfo` with the values and `ev=checks passed=<n> failed=<n>`.

## Goldens

`tests/golden/boot_<board>.png` and `error_crc_<board>.png` are screenshots. The boot golden shows the size and CRC of the test stage, so a change to `tests/stage2` needs new goldens. `ROBCO_UPDATE_GOLDEN=1` rewrites them. Look at the new images before you commit them.

## Timing

The key window test checks lower bounds only: a window with no key lasts at least `delay_ms` as seen from the host, and a key ends it at least 0.5 s early. Other QEMU runs on the same machine slow the guest down, so no test bounds a duration from above more tightly than that.
