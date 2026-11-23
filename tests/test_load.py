"""Loading the second stage: the fallback and every failure.

Bad images are the test stage with bytes patched, resealed where the case
needs a valid CRC. A failure must show its error line, print its markers,
never jump, and leave the maintenance port answering.

Set ROBCO_UPDATE_GOLDEN=1 to rewrite tests/golden/error_crc_<board>.png.
"""

import re

import pytest

from retros import (BACKUP, BOOT_COLD, BOOT_FALLBACK, IDLE, OFF_ABI, OFF_FILE_SIZE,
                    OFF_HDR_VERSION, OFF_LOAD_ADDR, OFF_MAGIC, PRIMARY, READY, TIMEOUT,
                    booted, bios_order, card, check_golden, dump, events, one, patched,
                    screenshot, stage2_checks, state)

ERROR_LINE = {
    "nofile": "ERROR E01 NOFILE: {path}: file not found",
    "io": "ERROR E02 IO: {path}: SD card or FAT volume not usable",
    "magic": "ERROR E03 MAGIC: {path}: not a boot image",
    "version": "ERROR E04 VERSION: {path}: unknown header version",
    "size": "ERROR E05 SIZE: {path}: size does not match the file",
    "bounds": "ERROR E06 BOUNDS: {path}: image does not fit its slot",
    "abi": "ERROR E07 ABI: {path}: wrong ABI version",
    "crc": "ERROR E08 CRC: {path}: image CRC does not match",
}
HALTED = "BOOT HALTED. Maintenance port is listening."
TWO_BOOTS = re.compile(r"(@RC stage=bios ev=boot [^\n]*\n[\s\S]*){2}")


def flip(image, offset=0x80):
    data = bytearray(image)
    data[offset] ^= 0x01
    return bytes(data)


def bad_image(stage2, code):
    if code == "crc":
        return flip(stage2)
    if code == "magic":
        return patched(stage2, OFF_MAGIC, "8s", b"ROM1\0\0\0\0")
    if code == "version":
        return patched(stage2, OFF_HDR_VERSION, "<H", 2)
    if code == "size":
        return patched(stage2, OFF_FILE_SIZE, "<I", len(stage2) + 4)
    if code == "bounds":
        return patched(stage2, OFF_LOAD_ADDR, "<I", 0x00210000)
    if code == "abi":
        return patched(stage2, OFF_ABI, "<I", 3 << 16)
    raise ValueError(code)


def check_stopped(vm, code, errors):
    """No jump, the shell marker, the screen lines, and the maintenance port answers."""
    vm.wait_for(IDLE, timeout=TIMEOUT)
    assert not events(vm, "jump"), vm.serial
    assert not events(vm, "load"), vm.serial
    assert not events(vm, "boot", "stage2"), vm.serial
    assert [(e["code"], e.get("path")) for e in events(vm, "error")] == errors, vm.serial
    shell = one(vm, "shell")
    assert (shell["reason"], shell["available"]) == (code, "0")
    st = state(vm)
    assert (st["phase"], st["error"]) == ("halted", code), st
    _, rows = dump(vm)
    for c, path in errors:
        assert ERROR_LINE[c].format(path=path) in rows, rows
    assert HALTED in rows, rows
    return rows


def test_fallback_when_primary_corrupt(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: stage2})
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        err = one(vm, "error")
        assert (err["code"], err["path"]) == ("crc", PRIMARY)
        fb = one(vm, "fallback")
        assert (fb["path"], fb["from"], fb["to"], fb["reason"]) == (BACKUP, PRIMARY, BACKUP, "crc")
        assert one(vm, "load")["path"] == BACKUP
        order = bios_order(vm)
        assert order.index("error") < order.index("fallback") < order.index("load") \
            < order.index("jump")
        stage2_checks(vm)
        info = one(vm, "bootinfo", "stage2")
        assert info["path"] == BACKUP
        assert int(info["flags"], 16) == BOOT_COLD | BOOT_FALLBACK


def test_fallback_when_primary_missing(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {BACKUP: stage2})
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "fallback")["reason"] == "nofile"
        stage2_checks(vm)
        assert int(one(vm, "bootinfo", "stage2")["flags"], 16) & BOOT_FALLBACK


@pytest.mark.parametrize("code", ["magic", "version", "size", "bounds", "abi", "crc"])
def test_bad_image(board, bios_image, stage2, need_mtools, tmp_path, code):
    bad = bad_image(stage2, code)
    sd = card(tmp_path, {PRIMARY: bad, BACKUP: bad})
    with booted(bios_image, board, sd) as vm:
        check_stopped(vm, code, [(code, PRIMARY), (code, BACKUP)])
        assert one(vm, "fallback")["reason"] == code


def test_both_missing(board, bios_image, need_mtools, tmp_path):
    sd = card(tmp_path, {})
    with booted(bios_image, board, sd) as vm:
        check_stopped(vm, "nofile", [("nofile", PRIMARY), ("nofile", BACKUP)])


def test_primary_bad_and_backup_bad(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: bad_image(stage2, "magic")})
    with booted(bios_image, board, sd) as vm:
        check_stopped(vm, "magic", [("crc", PRIMARY), ("magic", BACKUP)])
        assert one(vm, "fallback")["reason"] == "crc"


def test_no_card(board, bios_image):
    with booted(bios_image, board) as vm:
        check_stopped(vm, "io", [("io", PRIMARY)])
        assert one(vm, "error")["what"] == "sd"
        post = {m["name"]: m["result"] for m in events(vm, "post")}
        assert (post["sd"], post["fat"]) == ("absent", "absent"), vm.serial
        assert one(vm, "post_done")["result"] == "fail"
        assert not events(vm, "config")


def test_no_fat(board, bios_image, need_mtools, tmp_path):
    sd = tmp_path / "blank.img"
    sd.write_bytes(bytes(16 << 20))
    with booted(bios_image, board, sd) as vm:
        check_stopped(vm, "io", [("io", PRIMARY)])
        assert one(vm, "error")["what"] == "fat"
        post = {m["name"]: m["result"] for m in events(vm, "post")}
        assert (post["sd"], post["fat"]) == ("ok", "fail"), vm.serial


def test_golden_error_screen(board, bios_image, stage2, need_mtools, tmp_path):
    bad = flip(stage2)
    sd = card(tmp_path, {PRIMARY: bad, BACKUP: bad})
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        img = screenshot(vm, tmp_path)
    check_golden(img, f"error_crc_{board}")


def test_reboot_from_idle(board, bios_image, need_mtools, tmp_path):
    """reboot on the maintenance port restarts through the watchdog: a second boot marker."""
    sd = card(tmp_path, {})
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        vm.send("reboot\n", port=1)
        vm.wait_for("ev=reboot", timeout=TIMEOUT)
        vm.wait_for(TWO_BOOTS, timeout=TIMEOUT)
