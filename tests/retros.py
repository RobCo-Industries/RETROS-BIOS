"""Helpers for the RETROS-BIOS target tests."""

import contextlib
import os
import re
import struct
import subprocess
import tempfile
import time
import zlib

from PIL import Image

from harness.boot import boot, parse_markers
from harness.env import minimal_env
from harness.sdimage import make_sd_image

PRIMARY = "/ROBCO/MFBOOT.SYS"
BACKUP = "/ROBCO/MFBOOT.BAK"
CFG = "/ROBCO/BOOT.CFG"
SLOT = 0x00200000
PART_LBA = 2048
MACH = {"raspi0": 0xC42, "raspi1ap": 0xC42, "raspi2b": 0xC43, "raspi3b": 0xC43}
BOOT_FALLBACK = 1 << 0
BOOT_COLD = 1 << 2
TIMEOUT = 60
GOLDEN_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "golden")

# rimg header offsets, IMAGE_FORMAT.md.
OFF_MAGIC = 0x20
OFF_HDR_VERSION = 0x28
OFF_LOAD_ADDR = 0x30
OFF_FILE_SIZE = 0x34
OFF_CRC = 0x3C
OFF_ABI = 0x40

# A short key window keeps the tests that do not need it fast.
FAST_CFG = b"[bios]\ndelay_ms = 0\n"

READY = re.compile(r"@RC stage=stage2 ev=ready\r?\n")
IDLE = re.compile(r"@RC stage=bios ev=idle\r?\n")
KEY_WINDOW = re.compile(r"@RC stage=bios ev=key_window ms=\d+\r?\n")
KEY_WINDOW_END = re.compile(r"@RC stage=bios ev=key_window_end [^\n]*\n")
JUMP = re.compile(r"@RC stage=bios ev=jump [^\n]*\n")


def events(vm, name, stage="bios"):
    return [m for m in vm.markers() if m.get("stage") == stage and m.get("ev") == name]


def one(vm, name, stage="bios"):
    found = events(vm, name, stage)
    assert len(found) == 1, f"{stage} {name}: {len(found)} found\n{vm.serial}"
    return found[0]


def card(tmp_path, files, cfg=FAST_CFG, name="sd.img"):
    """An SD card image with `files` ({"/ROBCO/X": bytes}) and BOOT.CFG (None: none)."""
    content = {k.lstrip("/"): v for k, v in files.items()}
    if cfg is not None:
        content[CFG.lstrip("/")] = cfg
    if not content:
        content["ROBCO/"] = b""
    sd = tmp_path / name
    make_sd_image(sd, 16, content)
    return sd


def mbr_card(tmp_path, slots, mib=16, name="mbr.img"):
    """A card with up to four MBR entries, written here rather than by mksdimage.sh.

    `slots` has one item per MBR slot: None for an empty slot, or
    (type, start_lba, sectors, files). For a FAT type, `files` is a dict
    as in card() and the volume is formatted with mtools at its offset. A
    path ending in "/" makes a folder.
    For any other type, `files` is None and the blocks are filled with 0xA5.
    """
    sd = tmp_path / name
    entries = b""
    for slot in list(slots) + [None] * (4 - len(slots)):
        if slot is None:
            entries += bytes(16)
        else:
            ptype, lba, sectors, _ = slot
            entries += struct.pack("<B3sB3sII", 0, b"\xfe\xff\xff", ptype, b"\xfe\xff\xff",
                                   lba, sectors)
    with open(sd, "wb") as f:
        f.write(bytes(446) + entries + b"\x55\xaa")
        f.truncate(mib << 20)
    env = minimal_env(MTOOLS_SKIP_CHECK="1")

    def mt(*args):
        subprocess.run(list(args), check=True, env=env, capture_output=True)

    for i, slot in enumerate(slots):
        if slot is None:
            continue
        _, lba, sectors, files = slot
        if files is None:
            with open(sd, "r+b") as f:
                f.seek(lba * 512)
                f.write(b"\xa5" * (sectors * 512))
            continue
        img = f"{sd}@@{lba * 512}"
        mt("mformat", "-i", img, "-c", "1", "-v", f"VOL{i}", "-T", str(sectors), "-h", "64",
           "-s", "32", "-H", str(lba), "::")
        made = set()
        with tempfile.TemporaryDirectory(prefix="mbrcard") as tmp:
            for j, (path, data) in enumerate(sorted(files.items())):
                parts = [p for p in path.split("/") if p]
                if path.endswith("/"):  # a folder, not a file
                    parts.append("")
                for k in range(1, len(parts)):
                    d = "/".join(parts[:k])
                    if d not in made:
                        mt("mmd", "-i", img, "::" + d)
                        made.add(d)
                if not parts[-1]:
                    continue
                src = os.path.join(tmp, f"f{j}")
                with open(src, "wb") as fh:
                    fh.write(data)
                mt("mcopy", "-i", img, src, "::" + "/".join(parts))
    return sd


def reseal(data):
    data = bytearray(data)
    struct.pack_into("<I", data, OFF_CRC, 0)
    struct.pack_into("<I", data, OFF_CRC, zlib.crc32(data) & 0xFFFFFFFF)
    return bytes(data)


def patched(image, offset, fmt, value, seal=True):
    data = bytearray(image)
    struct.pack_into(fmt, data, offset, value)
    return reseal(data) if seal else bytes(data)


def header(image):
    return (struct.unpack_from("<I", image, OFF_FILE_SIZE)[0],
            struct.unpack_from("<I", image, OFF_CRC)[0])


@contextlib.contextmanager
def booted(bios_image, board, sd=None, extra_args=None):
    """Boots the BIOS with the maintenance port on serial 2."""
    with boot(bios_image, board, sd=sd, serial2=True, extra_args=extra_args) as vm:
        yield vm


SHELL_BANNER = "RETROS emergency shell. Type HELP for the commands."
SHELL_OPEN = re.compile(r"@RC stage=bios ev=shell [^\n]*available=1[^\n]*\n")
PROMPT = "RETROS> "
# QEMU's ds1338 model on the BSC1 bus, as RobCo-Core's test_rtc.py adds it.
RTC_DEVICE = ["-device", "ds1338,bus=i2c-bus.1,address=0x68"]


def open_shell(vm, paging=False):
    """Waits for the shell, then turns the pager off unless paging is True."""
    vm.wait_for(SHELL_OPEN, timeout=TIMEOUT)
    if not paging:
        reply = vm.script("paging off", timeout=TIMEOUT)
        assert reply.status == "ok", reply
    return one(vm, "shell")


def sh(vm, line, timeout=TIMEOUT):
    """Types one line on the console, as a person would, and waits for its shell_cmd marker.

    Returns (marker, output lines). The output lines are the PL011 text
    after the echoed line, markers left out.
    """
    before = len(events(vm, "shell_cmd"))
    start = len(vm.serial)
    # RobCo-Core's input queue holds 32 events and the console backend reads
    # up to 64 bytes per poll, so a long burst loses bytes. Type in chunks.
    data = line + "\r"
    for i in range(0, len(data), 16):
        vm.send(data[i:i + 16])
        time.sleep(0.05)
    deadline = time.monotonic() + timeout
    while len(events(vm, "shell_cmd")) <= before:
        if time.monotonic() > deadline:
            raise AssertionError(f"no shell_cmd for {line!r}\n{vm.serial[start:]}")
        time.sleep(0.05)
    marker = events(vm, "shell_cmd")[before]
    text = vm.serial[start:]
    lines = text.replace("\r", "").split("\n")
    out = []
    for ln in lines[1:]:
        if "ev=shell_cmd" in ln:
            break
        if not ln.startswith("@RC "):
            out.append(ln)
    return marker, out


def bios_order(vm):
    return [m["ev"] for m in vm.markers() if m.get("stage") == "bios"]


def dump(vm):
    """The screen text from the maintenance port: (header marker, rows)."""
    reply = vm.script("dump", timeout=TIMEOUT)
    assert reply.status == "ok", reply
    head = parse_markers(reply.lines[0] + "\n")[0]
    rows = [line[1:] for line in reply.lines if line.startswith("|")]
    return head, rows


def state(vm):
    reply = vm.script("state", timeout=TIMEOUT)
    assert reply.status == "ok" and len(reply.lines) == 1, reply
    return dict(field.split("=", 1) for field in reply.lines[0].split())


def screenshot(vm, tmp_path, name="screen"):
    ppm = tmp_path / f"{name}.ppm"
    vm.screendump(ppm)
    with Image.open(ppm) as img:
        return img.convert("RGB")


def check_golden(img, name):
    """Compares with tests/golden/<name>.png. ROBCO_UPDATE_GOLDEN=1 rewrites it."""
    path = os.path.join(GOLDEN_DIR, f"{name}.png")
    if os.environ.get("ROBCO_UPDATE_GOLDEN") == "1":
        os.makedirs(GOLDEN_DIR, exist_ok=True)
        img.save(path)
    with Image.open(path) as golden:
        assert golden.size == img.size, f"size differs from {path}"
        assert golden.convert("RGB").tobytes() == img.tobytes(), f"screen differs from {path}"


def stage2_checks(vm):
    """The test stage's check results as {name: marker}; asserts every check passed."""
    checks = {m["name"]: m for m in events(vm, "check", "stage2")}
    failed = [n for n, m in checks.items() if m["result"] != "pass"]
    assert not failed, f"failed checks {failed}\n{vm.serial}"
    summary = one(vm, "checks", "stage2")
    assert summary["failed"] == "0" and int(summary["passed"]) == len(checks), vm.serial
    return checks
