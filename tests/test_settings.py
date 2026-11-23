"""Settings from the [bios] section of /ROBCO/BOOT.CFG take effect, and a bad file means defaults."""

from retros import (SHELL_OPEN, JUMP, KEY_WINDOW, PRIMARY, READY, TIMEOUT, booted, card, dump, events,
                    one, stage2_checks)

AMBER = 1


def test_settings_take_effect(board, bios_image, stage2, need_mtools, tmp_path):
    cfg = b"[boot]\ndefault = UOS\n\n[bios]\ndelay_ms = 0\nbeep = off\nscheme = amber\n"
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=cfg)) as vm:
        vm.wait_for(JUMP, timeout=TIMEOUT)
        vm.wait_for(READY, timeout=TIMEOUT)
        c = one(vm, "config")
        assert (c["result"], c["applied"], c["bad"], c["unknown"]) == ("ok", "3", "0", "0")
        s = one(vm, "settings")
        assert (s["delay_ms"], s["beep"], s["scheme"]) == ("0", "0", "amber")
        assert one(vm, "beep")["state"] == "off"
        assert not events(vm, "audio"), vm.serial
        assert one(vm, "key_window")["ms"] == "0"
        assert "Press any key for the emergency shell" not in vm.lines()
        assert "Settings: delay 0 ms, beep off, scheme amber (/ROBCO/BOOT.CFG)" in vm.lines()
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["scheme"] == str(AMBER)


def test_scheme_on_screen(board, bios_image, need_mtools, tmp_path):
    cfg = b"[bios]\ndelay_ms = 0\nscheme = blue\n"
    with booted(bios_image, board, card(tmp_path, {}, cfg=cfg)) as vm:
        vm.wait_for(SHELL_OPEN, timeout=TIMEOUT)
        head, _ = dump(vm)
        assert head["scheme"] == "blue", head


def test_clamped_delay(board, bios_image, stage2, need_mtools, tmp_path):
    cfg = b"[bios]\ndelay_ms = 99999\nbeep = off\n"
    with booted(bios_image, board, card(tmp_path, {}, cfg=cfg)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        assert one(vm, "config")["clamped"] == "1"
        assert one(vm, "key_window")["ms"] == "10000"


def test_bad_file_means_defaults(board, bios_image, stage2, need_mtools, tmp_path):
    cfg = b"[bios]\nbeep = off\nscheme = amber\nthis line is not ini\n"
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=cfg)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        c = one(vm, "config")
        assert (c["result"], c["why"], c["line"]) == ("bad", "syntax", "4")
        s = one(vm, "settings")
        assert (s["delay_ms"], s["beep"], s["scheme"]) == ("1000", "1", "green")
        assert one(vm, "beep")["state"] == "played"
        assert "Settings: delay 1000 ms, beep on, scheme green (defaults)" in vm.lines()


def test_missing_file_means_defaults(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=None)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "config")["result"] == "missing"
        assert one(vm, "settings")["delay_ms"] == "1000"
