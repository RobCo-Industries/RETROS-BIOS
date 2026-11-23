"""The happy path: every step's markers in order, the handoff checked by the test stage.

Set ROBCO_UPDATE_GOLDEN=1 to rewrite tests/golden/boot_<board>.png from
the current run instead of comparing against it.
"""

import os
import re

import fixed_stage as fixed_stage_mod
from retros import (BOOT_COLD, BOOT_FALLBACK, JUMP, MACH, PART_LBA, PRIMARY, READY, SLOT,
                    TIMEOUT,
                    booted, card, check_golden, events, header, one, screenshot,
                    stage2_checks)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT, "boot", "BOOT.CFG"), "rb") as f:
    DEFAULT_CFG = f.read()

ORDER = ["boot", "init", "selfcheck", "input",
         "post", "post", "post", "post", "post", "mount", "post", "post", "post", "post_done",
         "config", "settings", "beep", "key_window", "key_window_end",
         "load", "bootinfo", "jump"]
POST_NAMES = ["memory", "video", "timer", "sd", "fat", "rng", "rtc", "audio"]
CHECKS = {"r0", "r1", "r2", "r3", "mode", "sctlr", "bootinfo_crc", "trust", "image_crc",
          "stage", "board", "memory", "fdt", "caps", "uart", "fb", "console", "boot_dev",
          "boot_flags", "boot_path", "cmdline"}

BOOT_TEXT = [
    "RETROS BIOS 0.1.0",
    "RBIOS-4.02.08.00 52EE5.E7.E8",
    "Copyright 2201-2203 Robco Ind.",
]
POST_ROWS = [
    "POWER-ON SELF TEST",
    "  MEMORY   OK     256 KiB at 0x00400000",
    "  VIDEO    OK     640x480x32",
    "  TIMER    OK     advancing",
    "  SD CARD  OK     block 0 read",
    f"  FAT      OK     part 0, LBA {PART_LBA}, 30720 blocks",
    "  RNG      OK     hardware",
    "  RTC      ABSENT no clock chip",
    "  AUDIO    OK     trace",
    "POST: 7 OK, 0 FAILED, 1 ABSENT",
    "Settings: delay 1000 ms, beep on, scheme green (/ROBCO/BOOT.CFG)",
    "Press any key for the emergency shell",
    "Loading /ROBCO/MFBOOT.SYS",
]


def happy(bios_image, board, stage2, tmp_path):
    sd = card(tmp_path, {PRIMARY: stage2}, cfg=DEFAULT_CFG)
    return booted(bios_image, board, sd)


def test_happy_path(board, bios_image, stage2, need_mtools, tmp_path):
    size, crc = header(stage2)
    with happy(bios_image, board, stage2, tmp_path) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        order = [m["ev"] for m in vm.markers() if m.get("stage") == "bios"]
        audio = [e for e in order if e == "audio"]
        assert [e for e in order if e != "audio"] == ORDER, vm.serial
        assert len(audio) == 6, vm.serial

        b = one(vm, "boot")
        assert b["version"] == "0.1.0"
        assert [m["name"] for m in events(vm, "post")] == POST_NAMES
        post = {m["name"]: m["result"] for m in events(vm, "post")}
        # QEMU has no RTC chip on the bus; everything else is present.
        assert post == {**{n: "ok" for n in POST_NAMES}, "rtc": "absent"}, vm.serial
        done = one(vm, "post_done")
        assert (done["result"], done["ok"], done["absent"], done["skipped"]) == ("ok", "7", "1", "0")
        mount = one(vm, "mount")
        assert (mount["part"], mount["lba"]) == ("0", str(PART_LBA))
        assert one(vm, "beep")["state"] == "played"
        assert one(vm, "key_window_end")["key"] == "0"
        assert not events(vm, "shell_request")
        assert not events(vm, "error") and not events(vm, "fallback")

        load = one(vm, "load")
        assert load == {"stage": "bios", "ev": "load", "path": PRIMARY, "size": str(size),
                        "crc": f"{crc:#010x}", "addr": f"{SLOT:#010x}", "name": "stage2"}
        assert one(vm, "jump")["entry"] == f"{SLOT:#010x}"

        # The test stage: every check passed, and the values themselves.
        checks = stage2_checks(vm)
        assert set(checks) == CHECKS
        assert one(vm, "handoff", "stage2")["trusted"] == "1"
        b2 = one(vm, "boot", "stage2")
        assert b2["entry_mode"] == "svc"
        for key in ("midr", "soc", "base", "board_rev", "mem"):
            assert b2[key] == b[key], key
        info = one(vm, "bootinfo", "stage2")
        assert info["prev_stage"] == "1"
        assert info["path"] == PRIMARY
        assert info["dev"] == "1" and info["part_lba"] == str(PART_LBA)
        assert int(info["flags"], 16) == BOOT_COLD
        assert not int(info["flags"], 16) & BOOT_FALLBACK
        assert info["fw_r1"] == f"{MACH[board]:#010x}"
        assert info["scheme"] == "0"
        assert checks["r1"]["value"] == f"{MACH[board]:#010x}"
        assert checks["r2"]["value"] == "0x00101000"
        assert checks["r3"]["value"] == "0x00100000"


def test_boot_text_on_serial(board, bios_image, stage2, need_mtools, tmp_path):
    with happy(bios_image, board, stage2, tmp_path) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        lines = vm.lines()
        b = one(vm, "boot")
        for text in BOOT_TEXT + POST_ROWS:
            assert text in lines, text
        assert f"Uppermem: {int(b['mem'], 16) // 1024} KB" in lines
        assert any(x.startswith("Board: Raspberry Pi ") and x.endswith(b["board_rev"])
                   for x in lines), lines
        assert one(vm, "selfcheck")["rc"] == "ok"
        assert not [x for x in lines if "self check" in x], lines


# One byte of the lore text, changed after the image was sealed.
LORE = b"RBIOS-4.02.08.00"


def test_selfcheck_finds_changed_byte(board, bios_image, stage2, need_mtools, tmp_path):
    """The image with one byte changed after sealing: the check fails, the boot goes on."""
    data = bytearray(open(bios_image, "rb").read())
    at = data.index(LORE)
    data[at] = ord("X")
    bad = tmp_path / "retros_changed.img"
    bad.write_bytes(bytes(data))
    with happy(str(bad), board, stage2, tmp_path) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "selfcheck")["rc"] == "crc", vm.serial
        assert "BIOS self check FAILED: crc. Booting anyway." in vm.lines(), vm.serial
        stage2_checks(vm)


def test_fixed_stage_is_pinned(fixed_stage):
    """The golden stage's bytes: a change in the generator or in rimg.py shows here."""
    assert len(fixed_stage) == fixed_stage_mod.SIZE
    assert header(fixed_stage) == (fixed_stage_mod.SIZE, fixed_stage_mod.CRC)
    assert fixed_stage_mod.sha256(fixed_stage) == fixed_stage_mod.SHA256


def test_golden_boot_screen(board, bios_image, fixed_stage, need_mtools, tmp_path):
    """The screen at the jump, with the fixed stage: no text from the toolchain.

    The fixed stage prints nothing, so the screen stays as the BIOS left it.
    """
    with happy(bios_image, board, fixed_stage, tmp_path) as vm:
        vm.wait_for(JUMP, timeout=TIMEOUT)
        img = screenshot(vm, tmp_path)
        serial = vm.serial
        loads = [(m.get("size"), m.get("crc")) for m in vm.markers() if m.get("ev") == "load"]
    # The only image size and CRC the scenario shows, in markers and in text,
    # are the fixed stage's.
    pinned = (str(fixed_stage_mod.SIZE), f"{fixed_stage_mod.CRC:#010x}")
    assert loads == [pinned], serial
    shown = re.findall(r"(\d+) bytes, CRC (0x[0-9a-f]+)", serial)
    assert shown == [pinned], serial
    check_golden(img, f"boot_{board}")
