"""The emergency shell: how it opens, every command, its error paths, and the pager.

Tests type on the console (the PL011, serial port 0) as a person would,
one line at a time, and wait for the shell_cmd marker of each line. The
maintenance port turns the pager off with `paging off`, except in the
pager test.

Set ROBCO_UPDATE_GOLDEN=1 to rewrite tests/golden/shell_help_<board>.png.
"""

import re
import time

import pytest

from retros import (BACKUP, BOOT_FALLBACK, PRIMARY, PROMPT, READY, SHELL_OPEN, TIMEOUT, booted,
                    break_chain, card, check_golden, dump, events, one, open_shell, screenshot, sh,
                    stage2_checks, state)

KEY_CFG = b"[bios]\ndelay_ms = 3000\nbeep = off\n"
KEY_WINDOW = re.compile(r"@RC stage=bios ev=key_window ms=\d+\r?\n")
TWO_BOOTS = re.compile(r"(@RC stage=bios ev=boot [^\n]*\n[\s\S]*){2}")
MORE = "-- more (any key, Q to stop) --"
# RAM the mailbox reports in QEMU: 512 MiB boards and 1 GiB boards, minus 64 MiB to the GPU.
RAM_END = {"raspi0": 0x1C000000, "raspi1ap": 0x1C000000, "raspi2b": 0x3C000000,
           "raspi3b": 0x3C000000}
PERIPH = {"raspi0": 0x20000000, "raspi1ap": 0x20000000, "raspi2b": 0x3F000000,
          "raspi3b": 0x3F000000}
HELP_LINES = 14


def flip(image, offset=0x80):
    data = bytearray(image)
    data[offset] ^= 0x01
    return bytes(data)


def key_shell(vm):
    """Opens the shell from the key window."""
    vm.wait_for(KEY_WINDOW, timeout=TIMEOUT)
    vm.send(" ")
    return open_shell(vm)


def test_help_and_bad_lines(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        m, out = sh(vm, "HELP")
        assert m == {"stage": "bios", "ev": "shell_cmd", "name": "help", "rc": "0"}
        assert len(out) == HELP_LINES, out
        assert out[1].startswith("HELP [command]") and out[-2].startswith("CLS"), out
        m, out = sh(vm, "help mem")
        assert out[0] == "MEM <addr> [len]", out
        m, out = sh(vm, "FOO bar")
        assert (m["name"], m["rc"]) == ("unknown", "unknown")
        assert out == ["Unknown command: FOO. Type HELP."], out
        m, out = sh(vm, "HELP FOO")
        assert (m["name"], m["rc"]) == ("help", "usage")
        assert out == ["No command named FOO. Type HELP."], out
        m, out = sh(vm, "INFO now")
        assert (m["name"], m["rc"]) == ("info", "usage")
        assert out == ["INFO takes no arguments. Type HELP."], out
        # A blank line gives no marker; the next command still works.
        vm.send("   \r")
        m, _ = sh(vm, "cls")
        assert (m["name"], m["rc"]) == ("cls", "0")
        # Backspace edits the line: "HELX", backspace, "P".
        m, out = sh(vm, "HELX\x7fP")
        assert m["name"] == "help" and len(out) == HELP_LINES
        # A line over 71 characters: the editor keeps the first 71.
        m, out = sh(vm, "X" * 90)
        assert out == ["Unknown command: XXXXXXXXXXXXXXXXXXXX. Type HELP."], out
        assert state(vm)["phase"] == "shell"


def test_line_edge(board, bios_image, need_mtools, tmp_path):
    """71 characters fit the row after the prompt; the 72nd is refused with a bell."""
    full = "DIR /" + "A" * 66
    assert len(PROMPT + full) == 79
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        sh(vm, "CLS")
        m, out = sh(vm, full)
        assert (m["name"], m["rc"]) == ("dir", "nofile"), out
        state(vm)
        _, rows = dump(vm)
        # The prompt and the line fill one row; the output starts on the next one.
        assert rows[0] == PROMPT + full, rows
        assert rows[1].startswith("Not found: /AAAA"), rows
        # A 72nd character: a bell on the serial line, nothing echoed, nothing kept.
        start = len(vm.serial)
        m, out = sh(vm, full + "B")
        assert (m["name"], m["rc"]) == ("dir", "nofile"), out
        typed = vm.serial[start:].split("\n", 1)[0].removeprefix(PROMPT)
        assert typed == full + "\a", repr(typed)
        # The line stays editable at the limit: backspace, then a new last character.
        start = len(vm.serial)
        m, out = sh(vm, full + "BC\x7fD")
        assert m["rc"] == "nofile", out
        typed = vm.serial[start:].split("\n", 1)[0].removeprefix(PROMPT)
        assert typed == full + "\a\a\b \bD", repr(typed)
        sh(vm, "CLS")
        # Backspace at column 0 does nothing: the prompt stays whole.
        m, out = sh(vm, "\x7f\x7fINFO")
        assert (m["name"], m["rc"]) == ("info", "0"), out
        state(vm)
        _, rows = dump(vm)
        assert rows[0] == PROMPT + "INFO", rows


def test_golden_help_screen(board, bios_image, need_mtools, tmp_path):
    """The help screen is fixed text: CLS, then HELP."""
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        sh(vm, "CLS")
        sh(vm, "HELP")
        # A script round trip: the shell is back in its input wait, the prompt is drawn.
        state(vm)
        _, rows = dump(vm)
        assert rows[0] == PROMPT + "HELP", rows
        assert rows[1].startswith("Commands."), rows
        assert rows[1 + HELP_LINES] == PROMPT.rstrip(), rows
        img = screenshot(vm, tmp_path)
    check_golden(img, f"shell_help_{board}")


def test_info_vol_post(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: flip(stage2)})
    with booted(bios_image, board, sd) as vm:
        shell = open_shell(vm)
        assert (shell["reason"], shell["available"]) == ("crc", "1")
        m, out = sh(vm, "INFO")
        assert m["rc"] == "0"
        assert out[0] == "RETROS BIOS 0.1.0"
        assert out[1].startswith("Board: ")
        assert out[2] == (f"Memory: {RAM_END[board] // 1024} KB ARM RAM at 0x00000000, "
                          f"peripherals at 0x{PERIPH[board]:08x}"), out
        assert out[3] == "Boot volume: part 0, LBA 2048, 30720 blocks", out
        assert out[4] == "Settings: delay 0 ms, beep on, scheme green (/ROBCO/BOOT.CFG)", out
        assert out[5] == "Last error: E08 CRC: image CRC does not match", out
        assert re.fullmatch(r"Uptime: \d+ h \d\d min \d\d s", out[6]), out
        m, out = sh(vm, "VOL")
        assert out == ["SLOT  TYPE  START LBA     BLOCKS  FAT  BOOT",
                       "   0  0x0e       2048      30720  yes  *",
                       "* is the boot volume."], out
        before = len(events(vm, "post"))
        st = state(vm)
        m, out = sh(vm, "POST")
        assert m["rc"] == "0"
        assert out[0] == "POWER-ON SELF TEST" and out[-1] == "POST: 7 OK, 0 FAILED, 1 ABSENT", out
        assert len(events(vm, "post")) == before + 8
        # The same card: the rerun leaves the state as it was.
        assert state(vm) == st, st
        # The card is mounted again: DIR still works.
        m, out = sh(vm, "DIR")
        assert m["rc"] == "0", out


def test_dir_and_type(board, bios_image, need_mtools, tmp_path):
    text = b"line one\r\nline\x01two\n"
    big = (b"0123456789abcde\n" * 1100)  # 17600 bytes, over the 16 KiB limit
    files = {"/ROBCO/b.txt": text, "/ROBCO/A.TXT": b"x", "/ROBCO/SUB/C.TXT": b"yz",
             "/ROBCO/BIG.TXT": big}
    with booted(bios_image, board, card(tmp_path, files)) as vm:
        open_shell(vm)
        m, out = sh(vm, "DIR")
        assert m["rc"] == "0"
        names = [ln.split()[0] for ln in out[:-1]]
        assert names == ["A.TXT", "b.txt", "BIG.TXT", "BOOT.CFG", "SUB"], out
        assert out[-1] == f"4 files, 1 folders, {1 + len(text) + len(big) + 20} bytes", out
        assert out[4].endswith("<DIR>"), out
        m, out = sh(vm, "dir robco/sub")
        assert out == [f"{'C.TXT':<60} {2:>10}", "1 files, 0 folders, 2 bytes"], out
        m, out = sh(vm, "DIR /NOPE")
        assert (m["rc"], out) == ("nofile", ["Not found: /NOPE"])
        m, out = sh(vm, "DIR /ROBCO/A.TXT")
        assert (m["rc"], out) == ("notdir", ["Not a folder: /ROBCO/A.TXT"])
        m, out = sh(vm, "TYPE /ROBCO/B.TXT")
        assert (m["rc"], out) == ("0", ["line one", "line.two"])
        m, out = sh(vm, "TYPE /ROBCO/NOPE.TXT")
        assert (m["rc"], out) == ("nofile", ["Not found: /ROBCO/NOPE.TXT"])
        m, out = sh(vm, "TYPE /ROBCO/SUB")
        assert (m["rc"], out) == ("isdir", ["Is a folder: /ROBCO/SUB"])
        m, out = sh(vm, "TYPE")
        assert (m["rc"], out) == ("usage", ["TYPE needs a path. Type HELP."])
        m, out = sh(vm, "TYPE /ROBCO/BIG.TXT")
        assert m["rc"] == "0"
        assert len(out) == 1024 + 1, len(out)
        assert out[-1] == f"-- cut: the file has {len(big)} bytes, 16384 shown --", out[-1]


def test_damaged_card(board, bios_image, need_mtools, tmp_path):
    """A folder and a file whose cluster chains break after the first cluster.

    The card has one-sector clusters: 16 directory entries per cluster.
    """
    files = {f"/ROBCO/SUB/F{i:02}.TXT": b"x" for i in range(20)}
    files["/ROBCO/LONG.TXT"] = b"0123456789abcde\n" * 128  # 2048 bytes, four clusters
    sd = card(tmp_path, files)
    break_chain(sd, "/ROBCO/SUB")
    break_chain(sd, "/ROBCO/LONG.TXT")
    with booted(bios_image, board, sd) as vm:
        open_shell(vm)
        m, out = sh(vm, "DIR /ROBCO/SUB")
        assert m["rc"] == "io", out
        # The first cluster holds ".", ".." and 14 files. FatFs moves to the next
        # entry inside the read of the 14th, so that read returns the error.
        assert [ln.split()[0] for ln in out[:-1]] == [f"F{i:02}.TXT" for i in range(13)], out
        assert out[-1] == "Read error after 13 entries: error -17", out
        m, out = sh(vm, "TYPE /ROBCO/LONG.TXT")
        assert (m["rc"], out) == ("io", ["Read error: /ROBCO/LONG.TXT: error -17"]), out
        # The rest of the card still reads.
        m, out = sh(vm, "DIR")
        assert m["rc"] == "0", out


def test_mem(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        # The BIOS image header at 0x8000 holds the magic "BOOT" at offset 0x20.
        m, out = sh(vm, "MEM 8020 10")
        assert m["rc"] == "0"
        assert len(out) == 1 and out[0].startswith("00008020: 42 4f 4f 54"), out
        assert out[0][59:63] == "BOOT", out
        m, out = sh(vm, "mem 0x8000")
        assert len(out) == 4 and [ln[:9] for ln in out] == \
            ["00008000:", "00008010:", "00008020:", "00008030:"], out
        m, out = sh(vm, "MEM 8000 1000")
        assert (m["rc"], len(out)) == ("0", 256)
        # A short last line keeps the text column aligned.
        m, out = sh(vm, "MEM 8020 5")
        assert out == [f"00008020: 42 4f 4f 54 00{' ' * 33}  BOOT."], out
        end = RAM_END[board]
        m, out = sh(vm, f"MEM {end - 16:x} 10")
        assert m["rc"] == "0" and out[0].startswith(f"{end - 16:08x}:"), out
        m, out = sh(vm, f"MEM {end - 16:x} 11")
        assert m["rc"] == "outside"
        assert out == [f"Outside RAM: RAM is 0x00000000 to 0x{end - 1:08x}."], out
        m, out = sh(vm, f"MEM {PERIPH[board]:x}")
        assert (m["rc"], out) == ("device", ["Device address refused: a read can change the "
                                             "device state."])
        m, out = sh(vm, "MEM 40000000 4")
        assert m["rc"] == "device"
        m, out = sh(vm, "MEM 0 1001")
        assert (m["rc"], out) == ("usage", ["Length must be 1 to 1000 (hex). Type HELP."])
        m, out = sh(vm, "MEM zz")
        assert (m["rc"], out) == ("usage", ["Bad address: zz. Type HELP."])


def test_boot_good_image_from_shell(board, bios_image, stage2, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {PRIMARY: stage2}, cfg=KEY_CFG)) as vm:
        key_shell(vm)
        assert not events(vm, "load")
        # A BOOT that starts the second stage ends with jump, not shell_cmd.
        vm.send("BOOT\r")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == PRIMARY
        stage2_checks(vm)
        assert one(vm, "bootinfo", "stage2")["path"] == PRIMARY


def test_boot_bad_image_returns_to_prompt(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: flip(stage2)})
    with booted(bios_image, board, sd) as vm:
        open_shell(vm)
        assert len(events(vm, "error")) == 2
        m, out = sh(vm, "BOOT")
        assert (m["name"], m["rc"]) == ("boot", "crc")
        assert "ERROR E08 CRC: /ROBCO/MFBOOT.SYS: image CRC does not match" in out, out
        assert "ERROR E08 CRC: /ROBCO/MFBOOT.BAK: image CRC does not match" in out, out
        assert len(events(vm, "fallback")) == 2
        assert not events(vm, "jump")
        # Back at the prompt.
        m, _ = sh(vm, "INFO")
        assert m["rc"] == "0"
        m, out = sh(vm, "BOOT /ROBCO/NONE.SYS")
        assert m["rc"] == "nofile"
        assert out[-1] == "ERROR E01 NOFILE: /ROBCO/NONE.SYS: file not found", out


def test_boot_explicit_path_has_no_fallback(board, bios_image, stage2, need_mtools, tmp_path):
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: stage2}, cfg=KEY_CFG)
    with booted(bios_image, board, sd) as vm:
        key_shell(vm)
        m, out = sh(vm, "BOOT /ROBCO/MFBOOT.SYS")
        assert m["rc"] == "crc", out
        assert not events(vm, "fallback"), vm.serial
        assert not events(vm, "load"), vm.serial
        assert out[-1] == "ERROR E08 CRC: /ROBCO/MFBOOT.SYS: image CRC does not match", out
        # Without the leading slash: the shell adds it.
        vm.send("BOOT ROBCO/MFBOOT.BAK\r")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == BACKUP
        assert not events(vm, "fallback")
        stage2_checks(vm)
        info = one(vm, "bootinfo", "stage2")
        assert int(info["flags"], 16) & BOOT_FALLBACK == 0, info


def test_failed_boot_keeps_shell_phase(board, bios_image, stage2, need_mtools, tmp_path):
    """A BOOT that fails gives the shell its phase back on the maintenance port."""
    sd = card(tmp_path, {PRIMARY: flip(stage2), BACKUP: stage2}, cfg=KEY_CFG)
    with booted(bios_image, board, sd) as vm:
        key_shell(vm)
        m, out = sh(vm, "BOOT /ROBCO/MFBOOT.SYS")
        assert m["rc"] == "crc", out
        st = state(vm)
        assert (st["phase"], st["error"]) == ("shell", "crc"), st
        reply = vm.script("shell", timeout=TIMEOUT)
        assert reply.status == "err shell is open", reply
        vm.send("BOOT /ROBCO/MFBOOT.BAK\r")
        vm.wait_for(READY, timeout=TIMEOUT)
        assert one(vm, "load")["path"] == BACKUP
        stage2_checks(vm)


def test_continue_refused_after_failure(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        shell = open_shell(vm)
        assert (shell["reason"], shell["available"]) == ("nofile", "1")
        m, out = sh(vm, "CONTINUE")
        assert (m["rc"], out) == ("refused",
                                  ["Nothing to continue: the boot stopped. Use BOOT or REBOOT."])
        assert not events(vm, "load")


def test_no_card_shell(board, bios_image):
    with booted(bios_image, board) as vm:
        shell = open_shell(vm)
        assert shell["reason"] == "io"
        m, out = sh(vm, "VOL")
        assert (m["rc"], out) == ("io", ["No SD card. Run POST to try again."])
        m, out = sh(vm, "DIR")
        assert (m["rc"], out) == ("novol", ["No boot volume."])
        m, out = sh(vm, "BOOT")
        assert m["rc"] == "io"
        assert out[-1] == "ERROR E02 IO: /ROBCO/MFBOOT.SYS: SD card or FAT volume not usable"
        m, out = sh(vm, "DIAG SD")
        assert (m["rc"], out) == ("0", ["  SD       ABSENT  no card"])


def test_boot_without_volume_records_error(board, fault_image, need_mtools, tmp_path):
    """The memtest fault image stops the POST before the FAT probe: no boot volume.

    BOOT then fails on the volume, and that IO error becomes the last error.
    """
    with booted(fault_image("memtest"), board, card(tmp_path, {})) as vm:
        open_shell(vm)
        assert state(vm)["error"] == "memtest"
        m, out = sh(vm, "BOOT")
        assert m["rc"] == "io", out
        assert out[-1] == "ERROR E02 IO: /ROBCO/MFBOOT.SYS: SD card or FAT volume not usable", out
        st = state(vm)
        assert (st["phase"], st["error"]) == ("shell", "io"), st
        m, out = sh(vm, "INFO")
        assert "Last error: E02 IO: SD card or FAT volume not usable" in out, out


def test_reboot(board, bios_image, need_mtools, tmp_path):
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm)
        vm.send("REBOOT\r")
        vm.wait_for("ev=reboot", timeout=TIMEOUT)
        vm.wait_for(TWO_BOOTS, timeout=TIMEOUT)
        assert events(vm, "shell_cmd")[-1]["name"] == "reboot"


def test_pager(board, bios_image, need_mtools, tmp_path):
    """With paging on, MEM 0 1000 stops after a screenful; Q ends the output."""
    with booted(bios_image, board, card(tmp_path, {})) as vm:
        open_shell(vm, paging=True)
        start = len(vm.serial)
        vm.send("MEM 0 1000\r")
        vm.wait_for(MORE, timeout=TIMEOUT)
        time.sleep(0.3)
        shown = [ln for ln in vm.serial[start:].replace("\r", "").split("\n")[1:]
                 if not ln.startswith("@RC")]
        # 30 rows: 29 lines, then the prompt on the last one.
        assert len(shown) == 30 and shown[-1] == MORE, shown
        assert not events(vm, "shell_cmd")
        vm.send(" ")
        vm.wait_for(re.compile(r"(-- more[\s\S]*){2}"), timeout=TIMEOUT)
        vm.send("q")
        vm.wait_for("ev=shell_cmd", timeout=TIMEOUT)
        assert "Stopped." in vm.lines()
        lines = [ln for ln in vm.lines() if re.match(r"[0-9a-f]{8}: ", ln)]
        assert len(lines) == 29 * 2, len(lines)
