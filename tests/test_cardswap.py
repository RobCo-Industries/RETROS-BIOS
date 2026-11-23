"""Card swaps in the shell: POST finds a card put in after the boot, or one pulled out.

QEMU changes the medium in the SD slot through QMP, so a test can pull
a card and put another one in while the shell runs.
"""

from retros import (EMPTY_SD_SLOT, PRIMARY, READY, TIMEOUT, booted, card, eject_card,
                    insert_card, mbr_card, one, open_shell, sh, stage2_checks, state)

POST_OK = "POST: 7 OK, 0 FAILED, 1 ABSENT"
VOL_OK = ["SLOT  TYPE  START LBA     BLOCKS  FAT  BOOT",
          "   0  0x0e       2048      30720  yes  *",
          "* is the boot volume."]


def fat_row(out):
    return next(ln for ln in out if ln.startswith("  FAT "))


def good_card_works(vm, stage2):
    """POST, VOL, DIR and INFO on the good card in the slot."""
    m, out = sh(vm, "POST")
    assert (m["rc"], out[-1]) == ("0", POST_OK), out
    assert state(vm)["post"] == "ok"
    m, out = sh(vm, "VOL")
    assert (m["rc"], out) == ("0", VOL_OK), out
    m, out = sh(vm, "DIR")
    assert m["rc"] == "0", out
    assert out[-1] == f"2 files, 0 folders, {len(stage2) + 20} bytes", out
    m, out = sh(vm, "INFO")
    assert out[3] == "Boot volume: part 0, LBA 2048, 30720 blocks", out


def test_card_put_in_after_boot_without_card(board, bios_image, stage2, need_mtools, tmp_path):
    good = card(tmp_path, {PRIMARY: stage2}, name="good.img")
    with booted(bios_image, board, extra_args=EMPTY_SD_SLOT) as vm:
        assert open_shell(vm)["reason"] == "io"
        before = state(vm)
        assert (before["post"], before["error"]) == ("fail", "io")
        # A rerun that finds the same hardware changes nothing.
        m, out = sh(vm, "POST")
        assert m["rc"] == "io", out
        assert state(vm) == before
        m, out = sh(vm, "BOOT")
        assert m["rc"] == "io", out
        insert_card(vm, good)
        good_card_works(vm, stage2)
        # The card goes out. The volume stays mounted until a card answers.
        eject_card(vm)
        m, out = sh(vm, "DIR")
        assert m["rc"] != "0", out
        assert out[-1].startswith("Cannot list /ROBCO: error "), out
        m, out = sh(vm, "POST")
        assert m["rc"] == "io", out
        assert fat_row(out).startswith("  FAT      FAILED old volume still mounted, error -"), out
        fat = [e for e in vm.markers() if e.get("ev") == "post" and e.get("name") == "fat"][-1]
        assert fat["what"] == "held", fat
        assert (state(vm)["post"], state(vm)["error"]) == ("fail", "io")
        m, out = sh(vm, "INFO")
        assert out[3:5] == ["Boot volume: part 0, LBA 2048, 30720 blocks",
                            "Boot volume card not answering. Put a card in and run POST."], out
        m, out = sh(vm, "BOOT")
        assert m["rc"] == "io", out
        assert out[-1] == "ERROR E02 IO: /ROBCO/MFBOOT.SYS: SD card or FAT volume not usable", out
        # The card goes back in: POST drops the old volume and mounts the card.
        insert_card(vm, good)
        good_card_works(vm, stage2)
        vm.send("BOOT\r")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == PRIMARY
        stage2_checks(vm)


def test_good_card_in_place_of_bad_one(board, bios_image, stage2, need_mtools, tmp_path):
    bad = mbr_card(tmp_path, [(0x83, 2048, 30720, None)], name="bad.img")
    good = card(tmp_path, {PRIMARY: stage2}, name="good.img")
    with booted(bios_image, board, bad) as vm:
        assert open_shell(vm)["reason"] == "io"
        m, out = sh(vm, "VOL")
        assert m["rc"] == "0" and out[-1] == "No boot volume is mounted.", out
        insert_card(vm, good)
        good_card_works(vm, stage2)
        vm.send("BOOT\r")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == PRIMARY
        stage2_checks(vm)
