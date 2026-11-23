"""The key window: a key on the console or the maintenance port sets the shell request."""

import time

from retros import (KEY_WINDOW, KEY_WINDOW_END, PRIMARY, READY, TIMEOUT, booted, card, events, one,
                    stage2_checks)

# A long window, so a key sent after the window marker always lands inside it.
WINDOW_MS = 3000
CFG = f"[bios]\ndelay_ms = {WINDOW_MS}\nbeep = off\n".encode()
NOT_BUILT = "Emergency shell is not built yet."


def test_console_key(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=CFG)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        t0 = time.monotonic()
        vm.send("x")
        vm.wait_for(KEY_WINDOW_END, timeout=TIMEOUT)
        # The key ends the window at once, not when the window runs out.
        assert time.monotonic() - t0 < WINDOW_MS / 1000 - 0.5
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "shell_request")["source"] == "console"
        assert one(vm, "key_window_end")["key"] == "1"
        assert NOT_BUILT in vm.lines()
        # The boot goes on.
        stage2_checks(vm)


def test_maintenance_port_key(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=CFG)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        assert vm.script("key select", timeout=TIMEOUT).status == "ok"
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "shell_request")["source"] == "maint"
        assert one(vm, "key_window_end")["key"] == "1"


def test_no_key(board, bios_image, stage2, need_mtools, tmp_path):
    """Without a key the window lasts at least delay_ms, then the boot goes on."""
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=CFG)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        t0 = time.monotonic()
        vm.wait_for(KEY_WINDOW_END, timeout=TIMEOUT)
        elapsed = time.monotonic() - t0
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "key_window")["ms"] == str(WINDOW_MS)
        assert one(vm, "key_window_end")["key"] == "0"
        assert not events(vm, "shell_request")
        assert NOT_BUILT not in vm.lines()
        # The guest waits on its own timer for the whole window. Seen from
        # the host, the end marker cannot come sooner (minus output latency).
        assert elapsed >= WINDOW_MS / 1000 - 0.1, elapsed
