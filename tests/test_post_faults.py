"""A failed memory test or a stopped timer stops the self test and the boot.

A stopped timer halts the BIOS: only the maintenance port answers. A
failed memory test opens the shell with a warning when the timer runs.
After a failed memory test the POST still runs the timer probe, so the
shell never opens on a timer nobody checked.

QEMU cannot stop its timer or break its RAM from outside, so these tests
boot fault images: the BIOS built under build/fault_<name>/ with one
RETROS_FAULT_* hook defined, or two for memtest_timer. The shipped
build/retros.img has none.
"""

import os
import subprocess

import pytest

from retros import (IDLE, PRIMARY, PROMPT, SHELL_BANNER, SHELL_OPEN, TIMEOUT, bios_order,
                    booted, card, dump, events, one, open_shell, sh, state)

HALTED = "BOOT HALTED. Maintenance port is listening."
MEMTEST_WARNING = "WARNING: memory test failed. Results may be wrong."
POST_NAMES = ["memory", "video", "timer", "sd", "fat", "rng", "rtc", "audio"]
# Only a fault image holds this marker text.
HOOK = b"fault_hook"

MEMTEST_LINE = "ERROR E16 MEMTEST: memory test failed"
TIMER_LINE = "ERROR E17 TIMER: system timer does not advance"
# Per image: the hooks, the probe that stops the POST, the probes that run and
# their results, the error and whether the shell opens.
FAULTS = {
    "timer": {
        "hooks": ["timer"],
        "stop": "timer",
        "ran": {"memory": "ok", "video": "ok", "timer": "fail"},
        "error": "timer",
        "line": TIMER_LINE,
        "shell": False,
    },
    "memtest": {
        "hooks": ["memtest"],
        "stop": "memory",
        "ran": {"memory": "fail", "timer": "ok"},
        "error": "memtest",
        "line": MEMTEST_LINE,
        "shell": True,
    },
    "memtest_timer": {
        "hooks": ["timer", "memtest"],
        "stop": "memory",
        "ran": {"memory": "fail", "timer": "fail"},
        "error": "memtest",
        "line": MEMTEST_LINE,
        "shell": False,
    },
}
# Markers of the steps that come after the self test. None may appear.
LATER = ["mount", "config", "settings", "beep", "audio", "key_window", "key_window_end",
         "shell_request", "fallback", "load", "bootinfo", "jump"]


@pytest.mark.parametrize("fault", sorted(FAULTS))
def test_fault_halts_before_later_probes(board, fault_image, stage2, need_mtools, tmp_path,
                                         fault):
    f = FAULTS[fault]
    sd = card(tmp_path, {PRIMARY: stage2})
    with booted(fault_image(fault), board, sd) as vm:
        vm.wait_for(SHELL_OPEN if f["shell"] else IDLE, timeout=TIMEOUT)
        assert [m["name"] for m in events(vm, "fault_hook")] == f["hooks"], vm.serial
        posts = events(vm, "post")
        assert [m["name"] for m in posts] == POST_NAMES, vm.serial
        for m in posts:
            if m["name"] in f["ran"]:
                assert m["result"] == f["ran"][m["name"]], m
            else:
                assert (m["result"], m["after"]) == ("skipped", f["stop"]), m
        results = list(f["ran"].values())
        ok, fail = results.count("ok"), results.count("fail")
        skipped = len(POST_NAMES) - len(results)
        done = one(vm, "post_done")
        assert (done["result"], done["ok"], done["fail"], done["absent"], done["skipped"]) == \
            ("fail", str(ok), str(fail), "0", str(skipped)), done
        # The first error is the one shown and recorded.
        assert one(vm, "error") == {"stage": "bios", "ev": "error", "code": f["error"]}
        for ev in LATER:
            assert not events(vm, ev), (ev, vm.serial)
        order = [m["ev"] for m in vm.markers() if m.get("stage") == "bios"]
        shell = one(vm, "shell")
        if f["shell"]:
            assert order[-3:] == ["post_done", "error", "shell"], order
            assert (shell["reason"], shell["available"]) == (f["error"], "1")
        else:
            assert order[-4:] == ["post_done", "error", "shell", "idle"], order
            assert (shell["reason"], shell["available"]) == (f["error"], "0")
            assert PROMPT not in vm.serial, vm.serial

        # The maintenance port still answers.
        st = state(vm)
        phase = "shell" if f["shell"] else "halted"
        assert (st["phase"], st["error"], st["post"]) == (phase, f["error"], "fail"), st
        _, rows = dump(vm)
        assert f["line"] in rows, rows
        if f["error"] == "memtest":
            assert TIMER_LINE not in rows, rows
        if f["shell"]:
            assert MEMTEST_WARNING in rows and SHELL_BANNER in rows, rows
            assert HALTED not in rows, rows
        else:
            assert HALTED in rows and SHELL_BANNER not in rows, rows
            assert MEMTEST_WARNING not in rows, rows
        assert "  AUDIO    SKIPPED" in rows, rows
        summary = f"POST: {ok} OK, {fail} FAILED, 0 ABSENT, {skipped} SKIPPED"
        assert summary in rows, rows
        # A second command after the first: the loop keeps serving.
        assert state(vm)["phase"] == phase
        # `shell` on the port cannot open a shell after a stopped timer.
        if not f["shell"]:
            reply = vm.script("shell", timeout=TIMEOUT)
            assert reply.status == "err timer stopped, no shell", reply



def test_shell_post_stopped_timer_halts(board, fault_image, need_mtools, tmp_path):
    """The timer_late image: the boot's POST passes, the shell's POST finds the timer stopped.

    The shell closes and the BIOS halts as at boot: only the maintenance port answers.
    """
    with booted(fault_image("timer_late"), board, card(tmp_path, {})) as vm:
        open_shell(vm)
        assert one(vm, "post_done")["result"] == "ok"
        vm.send("POST\r")
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert events(vm, "post_done")[-1]["result"] == "fail"
        assert events(vm, "error")[-1] == {"stage": "bios", "ev": "error", "code": "timer"}
        shells = events(vm, "shell")
        assert (shells[-1]["reason"], shells[-1]["available"]) == ("timer", "0"), shells
        # The shell ends without a shell_cmd marker for the POST.
        assert not events(vm, "shell_cmd"), vm.serial
        order = bios_order(vm)
        assert order[-4:] == ["post_done", "error", "shell", "idle"], order
        st = state(vm)
        assert (st["phase"], st["error"], st["post"]) == ("halted", "timer", "fail"), st
        _, rows = dump(vm)
        assert TIMER_LINE in rows and HALTED in rows, rows
        reply = vm.script("shell", timeout=TIMEOUT)
        assert reply.status == "err timer stopped, no shell", reply


def test_shell_post_memtest_keeps_shell(board, fault_image, need_mtools, tmp_path):
    """A failed memory test found by the shell's POST: the error and the warning, then the prompt."""
    with booted(fault_image("memtest"), board, card(tmp_path, {})) as vm:
        open_shell(vm)
        m, out = sh(vm, "POST")
        assert m["rc"] == "memtest", out
        assert out[-2:] == [MEMTEST_LINE, MEMTEST_WARNING], out
        st = state(vm)
        assert (st["phase"], st["error"]) == ("shell", "memtest"), st
        m, _ = sh(vm, "INFO")
        assert m["rc"] == "0"


def test_shipped_image_has_no_fault_hook(bios_image, fault_image):
    """The fault hooks exist only in the fault images."""
    def nm(image):
        elf = os.path.splitext(image)[0] + ".elf"
        return subprocess.run(["arm-none-eabi-nm", elf], check=True, capture_output=True,
                              text=True).stdout

    def raw(image):
        with open(image, "rb") as fh:
            return fh.read()

    syms = nm(bios_image)
    assert "memtest_read_hook" not in syms and "stuck_bit7" not in syms
    assert HOOK not in raw(bios_image)
    for fault in FAULTS:
        assert HOOK in raw(fault_image(fault)), fault
    for fault in ("memtest", "memtest_timer"):
        syms = nm(fault_image(fault))
        assert "memtest_read_hook" in syms and "stuck_bit7" in syms, fault
    # The tick fault is not a POST fault: it lives in DIAG TIMER.
    assert "fault_tick_drops" not in nm(bios_image)
    assert HOOK in raw(fault_image("tick"))
    assert "fault_tick_drops" in nm(fault_image("tick"))
    # The late timer fault counts POST runs only in its own image.
    assert "fault_post_runs" not in nm(bios_image)
    assert HOOK in raw(fault_image("timer_late"))
    assert "fault_post_runs" in nm(fault_image("timer_late"))
    # The PCM fault lives in DIAG AUDIO.
    assert "fault_pcm_rate_hz" not in nm(bios_image)
    assert HOOK in raw(fault_image("pcm"))
    assert "fault_pcm_rate_hz" in nm(fault_image("pcm"))
