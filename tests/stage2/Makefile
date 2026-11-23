# The test second stage: stands in for MFBootAgent in the target tests.
# Linked in the MFBootAgent slot. The tests copy it to the card as
# /ROBCO/MFBOOT.SYS. Not shipped.

ROBCO_CORE ?= ../../external/robco-core
IMAGE      := stage2
LINK_ADDR  := 0x00200000
SRCS       := main.c
CONFIG_H   := robco_config.h

include $(ROBCO_CORE)/mk/robco.mk
