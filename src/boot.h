/*
 * Boot errors and the boot fields of the boot info. Pure code: it builds
 * and runs on the host in the unit tests.
 */
#ifndef RETROS_BOOT_H
#define RETROS_BOOT_H

#include <stddef.h>
#include <stdint.h>

#include <robco/bootinfo.h>

/* The next stage, from BOOT_PROTOCOL.md and MEMORY_MAP.md. */
#define BIOS_PRIMARY    "/ROBCO/MFBOOT.SYS"
#define BIOS_BACKUP     "/ROBCO/MFBOOT.BAK"
#define BIOS_SLOT_BASE  0x00200000u
#define BIOS_SLOT_SIZE  0x00200000u
#define BIOS_NEXT_ABI   2u

/*
 * Error codes. 1 to 8 are the rimg_err_t values of the loader, with the
 * same numbers. BIOS_ERR_MEMTEST is the BIOS's own.
 */
#define BIOS_ERR_NOFILE   1
#define BIOS_ERR_IO       2
#define BIOS_ERR_MAGIC    3
#define BIOS_ERR_VERSION  4
#define BIOS_ERR_SIZE     5
#define BIOS_ERR_BOUNDS   6
#define BIOS_ERR_ABI      7
#define BIOS_ERR_CRC      8
#define BIOS_ERR_MEMTEST  16

/* Marker name of an error code, such as "crc". "unknown" for others. */
const char *bios_err_name(int code);
/* Short screen text of an error code, such as "image CRC does not match". */
const char *bios_err_text(int code);

/* Longest screen line, with the NUL: 79 characters, so a line never wraps on 80 columns. */
#define BIOS_LINE_MAX 80

/*
 * The screen line of an error: "ERROR E08 CRC: /ROBCO/MFBOOT.SYS: image
 * CRC does not match". path may be NULL or empty. The line is cut to
 * size - 1 characters. size must be at least 1.
 */
void bios_error_line(char *buf, size_t size, int code, const char *path);

/*
 * Writes the boot fields a loader owns into a block that
 * bootinfo_build() made, then reseals it: boot_dev, boot_part_lba, the
 * fallback flag in boot_flags and boot_path, cut to 63 characters and
 * NUL-padded. Other fields and flags stay as they are. cmdline stays
 * empty: the BIOS has none to pass.
 */
void bios_bootinfo_set_boot(robco_bootinfo_t *bi, uint32_t boot_dev, uint32_t part_lba,
                            int fallback, const char *path);

#endif
