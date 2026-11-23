#include "bootvol.h"

#include <robco/klibc.h>

const char *bootvol_fail(int rc, int err, int empty, char *buf, size_t size)
{
    switch (rc) {
    case PART_BOOT_ERR_DEVICE:
        k_snprintf(buf, size, "no partition");
        return "part";
    case PART_BOOT_ERR_NOFAT:
        if (empty) {
            k_snprintf(buf, size, "no partition");
            return "part";
        }
        k_snprintf(buf, size, "no FAT partition");
        return "nofat";
    case PART_BOOT_ERR_GPT:
        k_snprintf(buf, size, "GPT card, not supported");
        return "gpt";
    case PART_BOOT_ERR_MOUNT:
        k_snprintf(buf, size, "no FAT volume mounts");
        return "mount";
    case PART_BOOT_ERR_RELEASE:
        k_snprintf(buf, size, "volume does not unmount, error %d", err);
        return "held";
    default:
        k_snprintf(buf, size, "boot volume error %s", part_boot_strerror(rc));
        return part_boot_strerror(rc);
    }
}

int bootvol_release(int *mounted, int (*unmount)(void))
{
    int rc;

    if (!*mounted) {
        return 0;
    }
    rc = unmount();
    if (rc == 0) {
        *mounted = 0;
    }
    return rc;
}
