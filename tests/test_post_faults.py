"""A failed memory test or a stopped timer stops the self test and halts the boot.

QEMU cannot stop its timer or break its RAM from outside, so these tests
boot fault images: the BIOS built under build/fault_<name>/ with one
RETROS_FAULT_* hook defined. The shipped build/retros.img has none.
"""

import os
import subprocess

import pytest

from retros import IDLE, PRIMARY, TIMEOUT, booted, card, dump, events, one, state

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HALTED = "BOOT HALTED. Maintenance port is listening."
POST_NAMES = ["memory", "timer", "video", "sd", "fat", "rng", "rtc", "audio"]
# Only a fault image holds this marker text.
HOOK = b"fault_hook"

FAULTS = {
    "timer": {
        "define": "RETROS_FAULT_TIMER",
        "failed": "timer",
        "ok": ["memory"],
        "line": "ERROR E17 TIMER: system timer does not advance",
    },
    "memtest": {
        "define": "RETROS_FAULT_MEMTEST",
        "failed": "memory",
        "ok": [],
        "line": "ERROR E16 MEMTEST: memory test failed",
    },
}
# Markers of the steps that come after the self test. None may appear.
LATER = ["mount", "config", "settings", "beep", "audio", "key_window", "key_window_end",
         "shell_request", "fallback", "load", "bootinfo", "jump"]


@pytest.fixture(scope="session")
def fault_image(robco_build, build_dir):
    def get(name):
        out = os.path.join(build_dir, f"fault_{name}")
        flags = (f"BUILD={out}", f"EXTRA_CFLAGS=-D{FAULTS[name]['define']}=1")
        return robco_build(ROOT, target=f"{out}/retros.img", flags=flags,
                           image=os.path.join(out, "retros.img"))
    return get


@pytest.mark.parametrize("fault", sorted(FAULTS))
def test_fault_halts_before_later_probes(board, fault_image, stage2, need_mtools, tmp_path,
                                         fault):
    f = FAULTS[fault]
    sd = card(tmp_path, {PRIMARY: stage2})
    with booted(fault_image(fault), board, sd) as vm:
        vm.wait_for(IDLE, timeout=TIMEOUT)
        assert one(vm, "fault_hook")["name"] == fault
        posts = events(vm, "post")
        assert [m["name"] for m in posts] == POST_NAMES, vm.serial
        stop = POST_NAMES.index(f["failed"])
        for i, m in enumerate(posts):
            if i < stop:
                assert m["result"] == "ok", m
            elif i == stop:
                assert m["result"] == "fail", m
            else:
                assert (m["result"], m["after"]) == ("skipped", f["failed"]), m
        done = one(vm, "post_done")
        assert (done["result"], done["ok"], done["fail"], done["absent"], done["skipped"]) == \
            ("fail", str(stop), "1", "0", str(len(POST_NAMES) - stop - 1)), done
        assert one(vm, "error") == {"stage": "bios", "ev": "error", "code": fault}
        for ev in LATER:
            assert not events(vm, ev), (ev, vm.serial)
        order = [m["ev"] for m in vm.markers() if m.get("stage") == "bios"]
        assert order[-4:] == ["post_done", "error", "shell", "idle"], order
        shell = one(vm, "shell")
        assert (shell["reason"], shell["available"]) == (fault, "0")

        # The maintenance port still answers.
        st = state(vm)
        assert (st["phase"], st["error"], st["post"]) == ("halted", fault, "fail"), st
        _, rows = dump(vm)
        assert f["line"] in rows, rows
        assert HALTED in rows, rows
        assert "  AUDIO    SKIPPED" in rows, rows
        summary = f"POST: {stop} OK, 1 FAILED, 0 ABSENT, {len(POST_NAMES) - stop - 1} SKIPPED"
        assert summary in rows, rows
        # A second command after the first: the idle loop keeps serving.
        assert state(vm)["phase"] == "halted"



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
    syms = nm(fault_image("memtest"))
    assert "memtest_read_hook" in syms and "stuck_bit7" in syms
