# RETROS-BIOS, the first stage of the terminal chain. Built with the
# RobCo-Core library in external/robco-core.
#
#   make                      build/retros.img and the test second stage
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    the same, target tests on one board
#   make test-host            host unit tests, shell tests and the pin check
#   make test-target          QEMU target tests (BOARD=all by default)
#   make sdimage              build/sd.img: RETROS.IMG, config.txt,
#                             ROBCO/BOOT.CFG and a second stage as
#                             ROBCO/MFBOOT.SYS (STAGE2=<file>, default the
#                             test stage)
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make bootdir DIR=<path>   the files of build/sd.img for a real card,
#                             firmware included (FIRMWARE=0 leaves it out)
#   make check-pins           CI workflow pins and doc links match the submodule
#   make clean

ROBCO_CORE := external/robco-core
IMAGE      := retros
LINK_ADDR  := 0x8000
CONFIG_H   := src/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Every src/*.c builds into the image. PURE_SRCS, one per line and sorted,
# have no hardware access: the host tests link them.
PURE_SRCS  := \
              src/boot.c \
              src/bootlines.c \
              src/post.c \
              src/settings.c \

SRCS       := $(sort $(wildcard src/*.c))

# Host unit tests: tests/host/test_<name>.c with the pure files.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS)
HOST_INCLUDES := src

# The test second stage, built for tests only.
STAGE2_BUILD   = $(abspath $(BUILD))/stage2
STAGE2_DEFAULT = $(STAGE2_BUILD)/stage2.img
STAGE2        ?= $(STAGE2_DEFAULT)

# `make sdimage`. Set before the include: robco.mk reads them.
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/$(IMAGE).img
SDIMAGE_FILES := boot/BOOT.CFG:ROBCO/BOOT.CFG $(STAGE2):ROBCO/MFBOOT.SYS

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# Target tests boot the image built here; keep top-level targets serial.
.NOTPARALLEL:

.PHONY: stage2
all: stage2

stage2:
	$(MAKE) --no-print-directory -C tests/stage2 BUILD=$(STAGE2_BUILD)

$(STAGE2_DEFAULT): stage2

# ---- Python environment for the target tests --------------------------

VENV := .venv

.PHONY: venv
venv: $(VENV)/.installed

$(VENV)/.installed: $(ROBCO_CORE)/tests/requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/pip install -q -r $<
	touch $@

# ---- Tests ------------------------------------------------------------

BOARD ?= all

.PHONY: test test-target check-pins check-doc-links shell-tests
test: test-host
	$(MAKE) test-target

test-target: all venv
	$(VENV)/bin/pytest tests -v --board $(BOARD) --build-dir $(BUILD)

# DOC_ROOT: the tree whose Markdown links check-doc-links reads.
DOC_ROOT ?= .

check-pins: check-doc-links
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

check-doc-links:
	$(ROBCO_CORE)/tools/check_doc_links.sh $(DOC_ROOT) external/robco-core

shell-tests:
	tests/host/check_pins_test.sh
	tests/host/sdimage_test.sh

# Host unit tests come from mk/host.mk. The pin check and the shell tests run first.
test-host: check-pins shell-tests

# ---- Cards and QEMU -----------------------------------------------------

# Runs after SDIMAGE_FILES are copied, before the card is packed.
sdimage-stage::
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(SDIMAGE_STAGE) \
	    --kernel-name RETROS.IMG --no-firmware

.PHONY: run bootdir
RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(BUILD)/$(IMAGE).img \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img $(if $(filter 1,$(WINDOW)),--window)

# The files of `make sdimage`, plus the firmware. A ROBCO/BOOT.CFG already
# in DIR stays: it holds the owner's settings.
bootdir: $(BUILD)/$(IMAGE).img $(STAGE2)
	$(if $(strip $(DIR)),,$(error DIR is not set: make bootdir DIR=<path>))
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(DIR) --kernel-name RETROS.IMG \
	    $(if $(filter 0,$(FIRMWARE)),--no-firmware)
	mkdir -p $(DIR)/ROBCO
	[ -e $(DIR)/ROBCO/BOOT.CFG ] || cp boot/BOOT.CFG $(DIR)/ROBCO/BOOT.CFG
	cp $(STAGE2) $(DIR)/ROBCO/MFBOOT.SYS
