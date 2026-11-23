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
