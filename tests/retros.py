"""Helpers for the RETROS-BIOS target tests."""

import contextlib
import os
import re
import struct
import zlib

from PIL import Image

from harness.boot import boot, parse_markers
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
def booted(bios_image, board, sd=None):
    """Boots the BIOS with the maintenance port on serial 2."""
    with boot(bios_image, board, sd=sd, serial2=True) as vm:
        yield vm


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
