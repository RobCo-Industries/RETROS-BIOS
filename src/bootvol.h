/*
 * The boot volume, around RobCo-Core's fs_mount_boot(). Pure code: the
 * host tests link it. post_hw.c makes the call.
 */
#ifndef RETROS_BOOTVOL_H
#define RETROS_BOOTVOL_H

#include <stddef.h>

#include <robco/fs.h>
#include <robco/part.h>

/*
 * The FAT probe's failure for a PART_BOOT_* code other than PART_BOOT_OK.
 * Writes the POST row detail into buf and returns the `what` word of the
 * fat marker. err is fs_boot_t.last_err. empty is nonzero when the card
 * has no partition table and no FAT boot sector: part_scan() found no
 * entry. Only PART_BOOT_ERR_NOFAT reads it.
 */
const char *bootvol_fail(int rc, int err, int empty, char *buf, size_t size);

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

/*
 * 1 when the part_scan() entry p is the mounted volume vol: the same slot,
 * start block and size. A card put in since the mount can hold another
 * partition in the same slot. Both come from the one SD device.
 */
int bootvol_is(const fs_boot_t *vol, const part_info_t *p);

#endif
