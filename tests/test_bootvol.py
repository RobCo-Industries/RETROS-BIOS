"""Finding the boot volume on cards with more than one MBR entry.

RobCo-Core's fs_mount_boot() tries the FAT entries in slot order, then the
other entries, and the BIOS boots from the first one that holds
/ROBCO/MFBOOT.SYS or MFBOOT.BAK as a non-empty regular file. Extended and
GPT protective entries are never mounted. The cards are written by
mbr_card() in retros.py.
"""

import pytest

from retros import (BACKUP, CFG, FAST_CFG, SHELL_BANNER, SHELL_OPEN, PRIMARY, READY, TIMEOUT, booted, dump, events, mbr_card,
                    one, stage2_checks, state)

FAT16 = 0x0E
LINUX = 0x83
GPT = 0xEE


def boot_files(stage2):
    return {PRIMARY: stage2, CFG: FAST_CFG}


@pytest.mark.parametrize("first", ["linux", "empty"])
def test_fat_in_second_slot(board, bios_image, stage2, need_mtools, tmp_path, first):
    slot0 = (LINUX, 2048, 4096, None) if first == "linux" else None
    sd = mbr_card(tmp_path, [slot0, (FAT16, 8192, 24576, boot_files(stage2))])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        mount = one(vm, "mount")
        assert (mount["part"], mount["lba"], mount["blocks"]) == ("1", "8192", "24576")
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["part_lba"] == "8192"


def test_boot_files_on_second_fat(board, bios_image, stage2, need_mtools, tmp_path):
    other = {"/README.TXT": b"not a boot volume\n"}
    sd = mbr_card(tmp_path, [(FAT16, 2048, 8192, other),
                             (FAT16, 10240, 22528, boot_files(stage2))])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        mount = one(vm, "mount")
        assert (mount["part"], mount["lba"]) == ("1", "10240")
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["part_lba"] == "10240"
        assert one(vm, "bootinfo", "bios")["part_lba"] == "10240"


def test_boot_files_on_both_fat(board, bios_image, stage2, need_mtools, tmp_path):
    sd = mbr_card(tmp_path, [(FAT16, 2048, 8192, boot_files(stage2)),
                             (FAT16, 10240, 22528, boot_files(stage2))])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "mount")["part"] == "0"
        assert one(vm, "bootinfo", "stage2")["part_lba"] == "2048"


def test_no_fat_entry(board, bios_image, need_mtools, tmp_path):
    sd = mbr_card(tmp_path, [(LINUX, 2048, 30720, None)])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(SHELL_OPEN, timeout=TIMEOUT)
        post = {m["name"]: m for m in events(vm, "post")}
        assert (post["sd"]["result"], post["fat"]["result"]) == ("ok", "fail"), vm.serial
        assert post["fat"]["what"] == "nofat", vm.serial
        err = one(vm, "error")
        assert (err["code"], err["what"]) == ("io", "fat")
        assert not events(vm, "mount") and not events(vm, "jump")


def test_folder_named_like_boot_file_skipped(board, bios_image, stage2, need_mtools, tmp_path):
    folder = {PRIMARY + "/": b""}
    sd = mbr_card(tmp_path, [(FAT16, 2048, 8192, folder),
                             (FAT16, 10240, 22528, boot_files(stage2))])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "mount")["part"] == "1", vm.serial
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["part_lba"] == "10240"


def test_folder_named_like_boot_file_only(board, bios_image, need_mtools, tmp_path):
    sd = mbr_card(tmp_path, [(FAT16, 2048, 30720, {PRIMARY + "/": b""})])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(SHELL_OPEN, timeout=TIMEOUT)
        assert one(vm, "mount")["part"] == "0", vm.serial
        assert [(e["code"], e.get("path")) for e in events(vm, "error")] == \
            [("nofile", PRIMARY), ("nofile", BACKUP)], vm.serial
        assert not events(vm, "jump") and not events(vm, "load"), vm.serial
        st = state(vm)  # the maintenance port answers
        assert (st["phase"], st["error"]) == ("shell", "nofile"), st
        _, rows = dump(vm)
        assert "ERROR E01 NOFILE: " + BACKUP + ": file not found" in rows, rows
        assert SHELL_BANNER in rows, rows


def test_fat_behind_wrong_type_byte(board, bios_image, stage2, need_mtools, tmp_path):
    """The library's second pass: a FAT volume in an entry typed Linux still boots."""
    sd = mbr_card(tmp_path, [(LINUX, 2048, 30720, boot_files(stage2))])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "mount")["part"] == "0", vm.serial
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["part_lba"] == "2048"


def test_gpt_protective_entry(board, bios_image, need_mtools, tmp_path):
    """A GPT card: the protective entry is never mounted, the FAT probe names GPT."""
    sd = mbr_card(tmp_path, [(GPT, 1, 32767, None)])
    with booted(bios_image, board, sd) as vm:
        vm.wait_for(SHELL_OPEN, timeout=TIMEOUT)
        post = {m["name"]: m for m in events(vm, "post")}
        assert (post["sd"]["result"], post["fat"]["result"]) == ("ok", "fail"), vm.serial
        assert post["fat"]["what"] == "gpt", vm.serial
        err = one(vm, "error")
        assert (err["code"], err["what"]) == ("io", "fat")
        assert not events(vm, "mount") and not events(vm, "jump")
        _, rows = dump(vm)
        assert any(r.startswith("  FAT      FAILED GPT card, not supported") for r in rows), rows
