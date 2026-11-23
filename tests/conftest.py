import os
import sys

# RobCo-Core's harness: the --board option, the board fixture, need_mtools
# and robco_build. RETROS-BIOS adds --build-dir for the image under test.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tests"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
pytest_plugins = ["harness.plugin"]

import pytest  # noqa: E402


def pytest_addoption(parser):
    parser.addoption("--build-dir", default="build",
                     help="build directory, as BUILD in the Makefile")


@pytest.fixture(scope="session")
def build_dir(request):
    return os.path.join(ROOT, request.config.getoption("--build-dir"))


@pytest.fixture(scope="session")
def bios_image(robco_build, build_dir):
    """build/retros.img, built with the test stage by `make all`."""
    return robco_build(ROOT, flags=(f"BUILD={build_dir}",),
                       image=os.path.join(build_dir, "retros.img"))


@pytest.fixture(scope="session")
def stage2(bios_image, build_dir):
    """The bytes of the test second stage."""
    with open(os.path.join(build_dir, "stage2", "stage2.img"), "rb") as f:
        return f.read()


@pytest.fixture(scope="session")
def fixed_stage(build_dir):
    """The bytes of the fixed second stage, written to build/fixed_stage.img.

    The goldens boot it: its size and CRC do not depend on the toolchain.
    """
    import fixed_stage as fs
    data = fs.build()
    os.makedirs(build_dir, exist_ok=True)
    with open(os.path.join(build_dir, "fixed_stage.img"), "wb") as f:
        f.write(data)
    return data


# Fault images: the BIOS built again under build/fault_<name>/ with one hook.
FAULT_DEFINES = {"timer": "RETROS_FAULT_TIMER", "memtest": "RETROS_FAULT_MEMTEST",
                 "tick": "RETROS_FAULT_TICK"}


@pytest.fixture(scope="session")
def fault_image(robco_build, build_dir):
    def get(name):
        out = os.path.join(build_dir, f"fault_{name}")
        flags = (f"BUILD={out}", f"EXTRA_CFLAGS=-D{FAULT_DEFINES[name]}=1")
        return robco_build(ROOT, target=f"{out}/retros.img", flags=flags,
                           image=os.path.join(out, "retros.img"))
    return get
