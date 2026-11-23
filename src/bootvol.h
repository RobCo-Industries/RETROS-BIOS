/*
 * Choosing the boot volume among the entries part_scan() returns. Pure
 * code: the host tests link it. post_hw.c mounts the entries.
 */
#ifndef RETROS_BOOTVOL_H
#define RETROS_BOOTVOL_H

#include <robco/fs.h>
#include <robco/part.h>

/* What the FAT probe found on one entry. */
#define BOOTVOL_NOT_FAT  0  /* not a FAT entry: never mounted */
#define BOOTVOL_NO_MOUNT 1  /* a FAT entry that did not mount */
#define BOOTVOL_EMPTY    2  /* mounted, but no boot file (see bootvol_is_boot_file) */
#define BOOTVOL_BOOT     3  /* mounted and holds MFBOOT.SYS or MFBOOT.BAK as a boot file */

/* Nonzero for a superfloppy or an entry with one of the FAT type bytes of part.h. */
int bootvol_is_fat(const part_info_t *p);

/* Nonzero for a GPT protective entry. */
int bootvol_is_gpt(const part_info_t *p);

/*
 * Nonzero when the fs_stat() result of MFBOOT.SYS or MFBOOT.BAK makes the
 * volume a boot volume: a regular file of at least one byte. A folder of
 * that name or an empty file can never load, so the probe moves on.
 */
int bootvol_is_boot_file(const fs_stat_t *st);

/*
 * The entry to boot from, given the probe result of each of the n
 * entries in slot order: the first BOOTVOL_BOOT, else the first
 * BOOTVOL_EMPTY, so that the load reports the missing file against it.
 * Returns -1 when no entry mounted.
 */
int bootvol_pick(const int *state, unsigned n);

/*
 * The first step of the FAT probe: drops the volume an earlier run
 * mounted, so that the probe can mount a card under the same name.
 * *mounted is 1 while the file system library holds the volume, and
 * unmount is fs_unmount("sd") on the target. Returns 0 when no volume is
 * mounted any more. Otherwise returns the unmount error and leaves
 * *mounted at 1: the name is still taken and the probe must not mount.
 * The unmount syncs the card first, so it fails while no card answers.
 */
int bootvol_release(int *mounted, int (*unmount)(void));

#endif
