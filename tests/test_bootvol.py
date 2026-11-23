"""Finding the boot volume on cards with more than one MBR entry.

The BIOS tries the FAT entries in slot order and boots from the first one
that holds /ROBCO/MFBOOT.SYS or MFBOOT.BAK. Other entry types are never
mounted. The cards are written by mbr_card() in retros.py.
"""

import pytest

from retros import (CFG, FAST_CFG, IDLE, PRIMARY, READY, TIMEOUT, booted, events, mbr_card,
                    one, stage2_checks)

FAT16 = 0x0E
LINUX = 0x83


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
        vm.wait_for(IDLE, timeout=TIMEOUT)
        post = {m["name"]: m for m in events(vm, "post")}
        assert (post["sd"]["result"], post["fat"]["result"]) == ("ok", "fail"), vm.serial
        assert post["fat"]["what"] == "nofat", vm.serial
        err = one(vm, "error")
        assert (err["code"], err["what"]) == ("io", "fat")
        assert not events(vm, "mount") and not events(vm, "jump")
