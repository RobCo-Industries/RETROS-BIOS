/*
 * Choosing the boot volume among the entries part_scan() returns. Pure
 * code: the host tests link it. post_hw.c mounts the entries.
 */
#ifndef RETROS_BOOTVOL_H
#define RETROS_BOOTVOL_H

#include <robco/part.h>

/* What the FAT probe found on one entry. */
#define BOOTVOL_NOT_FAT  0  /* not a FAT entry: never mounted */
#define BOOTVOL_NO_MOUNT 1  /* a FAT entry that did not mount */
#define BOOTVOL_EMPTY    2  /* mounted, but no MFBOOT.SYS or MFBOOT.BAK in /ROBCO */
#define BOOTVOL_BOOT     3  /* mounted and holds MFBOOT.SYS or MFBOOT.BAK */

/* Nonzero for a superfloppy or an entry with one of the FAT type bytes of part.h. */
int bootvol_is_fat(const part_info_t *p);

/* Nonzero for a GPT protective entry. */
int bootvol_is_gpt(const part_info_t *p);

/*
 * The entry to boot from, given the probe result of each of the n
 * entries in slot order: the first BOOTVOL_BOOT, else the first
 * BOOTVOL_EMPTY, so that the load reports the missing file against it.
 * Returns -1 when no entry mounted.
 */
int bootvol_pick(const int *state, unsigned n);

#endif
