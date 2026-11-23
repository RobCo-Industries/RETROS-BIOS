#ifndef ROBCO_CONFIG_H
#define ROBCO_CONFIG_H

/* Preprocessor macros only: this file is also forced into asm and the linker script. */

#define RETROS_VERSION_MAJOR    0
#define RETROS_VERSION_MINOR    1
#define RETROS_VERSION_PATCH    0
#define RETROS_VERSION_STR      "0.1.0"

#define ROBCO_STAGE_NAME        "bios"

/* A first stage always cold-boots and ignores r3. See BOOT_PROTOCOL.md, trust rule. */
#define ROBCO_FIRST_STAGE       1

#define ROBCO_IMG_MAGIC         RIMG_MAGIC_BOOT
#define ROBCO_IMG_SUBTYPE       RIMG_BOOT_BIOS
#define ROBCO_IMG_ABI_VERSION   (2 << 16)
#define ROBCO_IMG_VERSION       ((RETROS_VERSION_MAJOR << 16) | (RETROS_VERSION_MINOR << 8) | \
                                 RETROS_VERSION_PATCH)
#define ROBCO_IMG_NAME          "RETROS"

/* BIOS range, 0x00008000 to 0x000FFFFF. The link fails if the image does not fit. */
#ifndef ROBCO_SLOT_END
#define ROBCO_SLOT_END          0x00100000
#endif

#endif
