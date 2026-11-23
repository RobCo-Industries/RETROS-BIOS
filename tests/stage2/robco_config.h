#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define ROBCO_STAGE_NAME        "stage2"

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_AGENT
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       1
#define ROBCO_IMG_NAME          "stage2"

/* MFBootAgent slot, 0x00200000 to 0x003FFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x00400000
#endif

#endif
