"""A second stage whose bytes are the same on every host, for the goldens.

The compiled test stage (tests/stage2) changes size and CRC with the
toolchain, and the boot screen prints both. CI runs in a container with its
own arm-none-eabi-gcc, so a golden must not show that stage. This one is a
few fixed ARM words sealed by RobCo-Core's tools/rimg.py, so its size and
CRC are constants.

Run as a script to write the image: python3 tests/fixed_stage.py OUT
"""

import hashlib
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tools"))

import rimg  # noqa: E402

# ARM (A32) code, ARMv6 and later. It prints nothing, so the screen stays
# as the BIOS left it.
WORDS = [
    0xF10C00C0,  # cpsid if    mask IRQ and FIQ
    0xE320F003,  # wfi         wait for an interrupt (ARMv6K hint)
    0xEAFFFFFD,  # b   wfi     back to the wfi, forever
]
NAME = "fixed"
LOAD_ADDR = 0x00200000  # MFBootAgent slot base
SUBTYPE = 4             # RIMG_BOOT_AGENT
ABI_VERSION = 2 << 16   # BOOT ABI major 2

# Pinned: a change in WORDS, in the fields above or in rimg.py shows here.
SIZE = 108
CRC = 0x457EFE67
SHA256 = "94953799f88da7aa69e48449761d59a66f95908529e30c2c04c211d61ffba78d"


def build():
    payload = struct.pack(f"<{len(WORDS)}I", *WORDS)
    return rimg.pack(payload, "BOOT", SUBTYPE, LOAD_ADDR, name=NAME,
                     abi_version=ABI_VERSION)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


if __name__ == "__main__":
    data = build()
    with open(sys.argv[1], "wb") as f:
        f.write(data)
    print(f"size={len(data)} crc={struct.unpack_from('<I', data, 0x3C)[0]:#010x} "
          f"sha256={sha256(data)}")
