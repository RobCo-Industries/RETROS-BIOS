#include "bootvol.h"

int bootvol_is_fat(const part_info_t *p)
{
    if (p->superfloppy) {
        return 1;
    }
    switch (p->type) {
    case PART_TYPE_FAT12:
    case PART_TYPE_FAT16_SM:
    case PART_TYPE_FAT16:
    case PART_TYPE_FAT32:
    case PART_TYPE_FAT32_LBA:
    case PART_TYPE_FAT16_LBA:
        return 1;
    default:
        return 0;
    }
}

int bootvol_is_gpt(const part_info_t *p)
{
    return !p->superfloppy && p->type == PART_TYPE_GPT;
}

int bootvol_is_boot_file(const fs_stat_t *st)
{
    return !st->is_dir && (st->attr & FS_ATTR_DIR) == 0 && st->size > 0;
}

int bootvol_pick(const int *state, unsigned n)
{
    int empty = -1;
    unsigned i;

    for (i = 0; i < n; i++) {
        if (state[i] == BOOTVOL_BOOT) {
            return (int)i;
        }
        if (state[i] == BOOTVOL_EMPTY && empty < 0) {
            empty = (int)i;
        }
    }
    return empty;
}
