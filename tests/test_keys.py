"""The key window: a key on the console or the maintenance port opens the shell."""

import time

from retros import (FAST_CFG, KEY_WINDOW, KEY_WINDOW_END, PRIMARY, READY, SHELL_BANNER,
                    SHELL_OPEN, TIMEOUT, bios_order, booted, card, events, one, open_shell, sh,
                    stage2_checks)

# A long window, so a key sent after the window marker always lands inside it.
WINDOW_MS = 3000
CFG = f"[bios]\ndelay_ms = {WINDOW_MS}\nbeep = off\n".encode()


def test_console_key(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=CFG)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        t0 = time.monotonic()
        vm.send("x")
        vm.wait_for(KEY_WINDOW_END, timeout=TIMEOUT)
        # The key ends the window at once, not when the window runs out.
        assert time.monotonic() - t0 < WINDOW_MS / 1000 - 0.5
        shell = open_shell(vm)
        assert (shell["reason"], shell["available"]) == ("key", "1")
        assert one(vm, "shell_request")["source"] == "console"
        assert one(vm, "key_window_end")["key"] == "1"
        assert SHELL_BANNER in vm.lines()
        # The shell holds the boot until CONTINUE.
        assert not events(vm, "load"), vm.serial
        marker, out = sh(vm, "continue")
        assert marker["rc"] == "0" and out == ["Going on with the boot."], out
        vm.wait_for(READY, timeout=TIMEOUT)
        order = bios_order(vm)
        assert order.index("shell") < order.index("shell_cmd") < order.index("load"), order
        stage2_checks(vm)


def test_maintenance_port_key(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=CFG)) as vm:
        vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
        assert vm.script("key select", timeout=TIMEOUT).status == "ok"
        shell = open_shell(vm)
        assert shell["reason"] == "key"
        assert one(vm, "shell_request")["source"] == "maint"
        assert one(vm, "key_window_end")["key"] == "1"
        sh(vm, "CONTINUE")
        vm.wait_for(READY, timeout=TIMEOUT)


def test_shell_command_with_zero_window(board, bios_image, stage2, need_mtools, tmp_path):
    """`shell` on the maintenance port, sent during the self test, opens the shell with delay_ms = 0."""
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=FAST_CFG)) as vm:
        vm.wait_for("ev=input ", timeout=TIMEOUT)
        assert vm.script("shell", timeout=TIMEOUT).status == "ok"
        shell = open_shell(vm)
        assert (shell["reason"], shell["available"]) == ("maint", "1")
        assert one(vm, "shell_request")["source"] == "maint"
        assert one(vm, "key_window_end")["key"] == "1"
        # A second request while the shell is open is refused.
        assert vm.script("shell", timeout=TIMEOUT).status == "err shell is open"
        sh(vm, "CONTINUE")
        vm.wait_for(READY, timeout=TIMEOUT)
        stage2_checks(vm)


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
        assert not events(vm, "shell")
        assert SHELL_BANNER not in vm.lines()
        # The guest waits on its own timer for the whole window. Seen from
        # the host, the end marker cannot come sooner (minus output latency).
        assert elapsed >= WINDOW_MS / 1000 - 0.1, elapsed


def test_zero_window(board, bios_image, stage2, need_mtools, tmp_path):
    """delay_ms = 0: both window markers, in order, no prompt, and the boot goes on."""
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=FAST_CFG)) as vm:
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "key_window")["ms"] == "0"
        assert one(vm, "key_window_end")["key"] == "0"
        order = [e for e in bios_order(vm) if e != "audio"]
        start = order.index("key_window")
        assert order[start:start + 3] == ["key_window", "key_window_end", "load"], vm.serial
        assert not events(vm, "shell_request")
        assert "Press any key for the emergency shell" not in vm.lines()
        stage2_checks(vm)
