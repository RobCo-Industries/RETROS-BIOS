"""DIAG: every test on every board, the default windows, and real faults.

QEMU has no clock chip on the bus unless a test adds RobCo-Core's ds1338
model. The memory fault comes from the memtest fault image, whose read
hook sets bit 7 in the word at 0x00400100.
"""

import re

from retros import (PRIMARY, RTC_DEVICE, TIMEOUT, booted, card, events, one, open_shell,
                    screenshot, sh)

ROWS_NO_RTC = ["MEM", "SD", "VIDEO", "AUDIO", "TIMER", "RNG", "RTC"]
VIDEO_PATTERN = re.compile(r"@RC stage=bios ev=video_pattern [^\n]*\n")


def table(out):
    """{name: (result, detail)} from the rows of a DIAG ALL table."""
    rows = {}
    for ln in out:
        m = re.match(r"  (\S+) +(OK|FAILED|ABSENT|SKIPPED) *(.*)$", ln)
        if m:
            rows[m.group(1)] = (m.group(2), m.group(3))
    return rows


def diag_markers(vm):
    return {m["name"]: m for m in events(vm, "diag")}


def check_timer(t):
    """A quiet run: every tick seen, and within the limit diagcalc.h derives."""
    assert (t["result"], t["ticks"], t["period_us"], t["missed"]) == ("ok", "200", "10000", "0"), t
    assert t["periods"] == "200", t
    # Without a missed tick, the two stamps differ by less than one period.
    assert abs(int(t["ppm"])) < 5000, t
    assert abs(int(t["us"]) - 2000000) < 10000, t


def check_all(vm, rtc):
    m, out = sh(vm, "DIAG ALL", timeout=180)
    assert m["rc"] == "0", "\n".join(out)
    assert out[0] == "DIAGNOSTICS", out
    rows = table(out)
    assert list(rows) == ROWS_NO_RTC, out
    want_rtc = "OK" if rtc else "ABSENT"
    for name, (result, _) in rows.items():
        assert result == (want_rtc if name == "RTC" else "OK"), (name, out, vm.serial[-3000:])
    assert out[-1] == f"DIAG: {6 + rtc} OK, 0 FAILED, {1 - rtc} ABSENT, 0 SKIPPED", out
    d = diag_markers(vm)
    assert set(d) == {"mem", "sd", "video", "audio", "timer", "rng", "rtc"}, d
    # ALL tests the first 16 MiB of the default window, from 0x00400000.
    assert (d["mem"]["base"], d["mem"]["size"]) == ("0x00400000", "0x01000000"), d["mem"]
    assert (d["sd"]["lba"], d["sd"]["blocks"], d["sd"]["errors"]) == ("2048", "256", "0")
    assert int(d["sd"]["kib_s"]) > 0
    assert d["audio"]["backend"] == "trace" and d["audio"]["pcm"] == "0", d["audio"]
    check_timer(d["timer"])
    assert (d["rng"]["words"], d["rng"]["repeats"], d["rng"]["bits"]) == ("1024", "0", "32768")
    assert abs(int(d["rng"]["ones"]) - 16384) <= 512, d["rng"]
    done = one(vm, "diag_done")
    assert (done["ok"], done["fail"], done["absent"]) == (str(6 + rtc), "0", str(1 - rtc))
    return d, out


def test_diag_all(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        d, _ = check_all(vm, rtc=0)
        assert d["rtc"]["result"] == "absent"


def test_diag_all_with_clock_chip(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {}), extra_args=RTC_DEVICE) as vm:
        open_shell(vm)
        d, out = check_all(vm, rtc=1)
        assert d["rtc"]["chip"] == "ds1307", d["rtc"]
        assert re.fullmatch(r"20\d\d-\d\d-\d\dT\d\d:\d\d:\d\d", d["rtc"]["time"]), d["rtc"]
        assert table(out)["RTC"][1].endswith("UTC, DS1307"), out


def test_diag_list_and_errors(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        m, out = sh(vm, "DIAG")
        assert m["rc"] == "0" and len(out) == 10, out
        m, out = sh(vm, "DIAG FOO")
        assert (m["rc"], out) == ("usage", ["Unknown test: FOO. Type HELP."])
        # Below 0x00400000 the BIOS lives: refused, nothing written.
        m, out = sh(vm, "DIAG MEM 8000 1000")
        assert m["rc"] == "0"
        assert out == ["  MEM      SKIPPED window not allowed, see HELP DIAG"], out
        assert diag_markers(vm)["mem"]["reason"] == "window"
        m, out = sh(vm, "DIAG MEM 400000 3")
        assert m["rc"] == "usage"


def test_diag_mem_small_window(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        m, out = sh(vm, "DIAG MEM 500000 2000")
        assert m["rc"] == "0"
        assert out == ["  MEM      OK      8192 bytes at 0x00500000, 4 patterns"], out
        d = diag_markers(vm)["mem"]
        assert (d["result"], d["base"], d["size"]) == ("ok", "0x00500000", "0x00002000"), d


def test_diag_mem_finds_stuck_bit(board, fault_image, need_mtools, tmp_path):
    """The memtest fault image: the POST fails, the shell opens, DIAG MEM finds the word."""
    with booted(fault_image("memtest"), board, card(tmp_path, {})) as vm:
        shell = open_shell(vm)
        assert shell["reason"] == "memtest"
        m, out = sh(vm, "DIAG MEM 400000 1000")
        assert m["rc"] == "fail", out
        d = diag_markers(vm)["mem"]
        assert (d["result"], d["pattern"], d["addr"]) == ("fail", "addr", "0x00400100"), d
        assert (d["expected"], d["actual"]) == ("0x00400100", "0x00400180"), d
        assert out == ["  MEM      FAILED  addr at 0x00400100: 0x00400180, not 0x00400100"], out
        # A window that does not hold the bad word passes.
        m, out = sh(vm, "DIAG MEM 401000 1000")
        assert m["rc"] == "0", "\n".join(out)


def test_diag_video_pattern(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        vm.send("DIAG VIDEO\r")
        vm.wait_for(VIDEO_PATTERN, timeout=TIMEOUT)
        img = screenshot(vm, tmp_path, "pattern")
        # Bars: white at the left, blue in the seventh eighth; the border one pixel in.
        assert img.getpixel((20, 100)) == (255, 255, 255)
        assert img.getpixel((7 * 80 - 20, 100)) == (0, 0, 255)
        assert img.getpixel((3 * 80 + 20, 100)) == (0, 255, 0)
        assert img.getpixel((7 * 80 + 20, 1)) == (255, 255, 255)
        assert img.getpixel((7 * 80 + 20, 0)) == (0, 0, 0)
        assert img.getpixel((7 * 80 + 20, 478)) == (255, 255, 255)
        assert img.getpixel((7 * 80 + 20, 120)) == (96, 96, 96)
        vm.wait_for("ev=shell_cmd", timeout=TIMEOUT)
        d = diag_markers(vm)["video"]
        assert (d["result"], d["width"], d["height"], d["key"]) == ("ok", "640", "480", "0"), d
        # The console text is back.
        img = screenshot(vm, tmp_path, "after")
        assert img.getpixel((20, 100)) != (255, 255, 255)


def test_diag_input(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        vm.send("DIAG INPUT\r")
        vm.wait_for("Q or 10 seconds", timeout=TIMEOUT)
        vm.send("a\x1b[A")
        vm.wait_for("KEY  1 up", timeout=TIMEOUT)
        vm.send("q")
        vm.wait_for("ev=shell_cmd", timeout=TIMEOUT)
        lines = vm.lines()
        assert "CHAR 0x61 a" in lines and "KEY  1 up" in lines and "CHAR 0x71 q" in lines
        d = diag_markers(vm)["input"]
        assert (d["events"], d["quit"]) == ("3", "1"), d


def test_diag_timer(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        # QEMU shares the host with other runs. When the guest gets no CPU
        # time for a while, one reading can fall outside the bound. The
        # bound and the diagnostic stay as they are: the test passes when
        # one of three runs is inside it, and fails when all three are out.
        fails = []
        for _ in range(3):
            m, out = sh(vm, "DIAG TIMER")
            t = diag_markers(vm)["timer"]
            try:
                assert m["rc"] == "0", out
                check_timer(t)
            except AssertionError as e:
                fails.append(str(e))
                continue
            assert out == [f"  TIMER    OK      {t['ppm']} ppm over 200 ticks, 0 missed"], out
            return
        raise AssertionError(f"DIAG TIMER out of bounds in 3 runs: {fails}")


def test_diag_timer_finds_lost_ticks(board, fault_image, need_mtools, tmp_path):
    """The tick fault image: the callback loses every tenth tick interrupt."""
    with booted(fault_image("tick"), board, card(tmp_path, {})) as vm:
        open_shell(vm)
        assert one(vm, "fault_hook")["name"] == "tick"
        m, out = sh(vm, "DIAG TIMER")
        assert m["rc"] == "fail", out
        t = diag_markers(vm)["timer"]
        assert (t["result"], t["ticks"]) == ("fail", "200"), t
        # 200 callbacks over about 222 periods: about -99000 ppm and 22 missed.
        assert 20 <= int(t["missed"]) <= 24, t
        assert -110000 < int(t["ppm"]) < -90000, t
        assert out[0].startswith("  TIMER    FAILED  -"), out
