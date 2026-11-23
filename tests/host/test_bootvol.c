/* Choosing the boot volume: which MBR entries count as FAT, and which one wins. */

#include "bootvol.h"
#include "rc_test.h"

static part_info_t entry(uint8_t type)
{
    part_info_t p;

    memset(&p, 0, sizeof(p));
    p.type = type;
    return p;
}

TEST(fat_types)
{
    static const uint8_t fat[] = { PART_TYPE_FAT12, PART_TYPE_FAT16_SM, PART_TYPE_FAT16,
                                   PART_TYPE_FAT32, PART_TYPE_FAT32_LBA, PART_TYPE_FAT16_LBA };
    static const uint8_t other[] = { 0x00, 0x07, 0x83, PART_TYPE_EXTENDED, PART_TYPE_EXT_LBA,
                                     PART_TYPE_GPT };
    part_info_t p;
    size_t i;

    for (i = 0; i < sizeof(fat); i++) {
        p = entry(fat[i]);
        CHECK(bootvol_is_fat(&p));
    }
    for (i = 0; i < sizeof(other); i++) {
        p = entry(other[i]);
        CHECK(!bootvol_is_fat(&p));
    }
    /* A card with no table: part_scan() gives one superfloppy entry with type 0. */
    p = entry(0);
    p.superfloppy = 1;
    CHECK(bootvol_is_fat(&p));
    CHECK(!bootvol_is_gpt(&p));
    p = entry(PART_TYPE_GPT);
    CHECK(bootvol_is_gpt(&p));
}

TEST(empty_table)
{
    CHECK_EQ(bootvol_pick(NULL, 0), -1);
}

TEST(fat_first)
{
    int s[] = { BOOTVOL_BOOT, BOOTVOL_NOT_FAT };

    CHECK_EQ(bootvol_pick(s, 2), 0);
}

TEST(fat_second_after_non_fat)
{
    int s[] = { BOOTVOL_NOT_FAT, BOOTVOL_BOOT };

    CHECK_EQ(bootvol_pick(s, 2), 1);
}

/* part_scan() skips an empty slot: FAT in slot 2 comes back as the only entry. */
TEST(fat_second_after_empty_slot)
{
    int s[] = { BOOTVOL_BOOT };

    CHECK_EQ(bootvol_pick(s, 1), 0);
}

TEST(boot_files_on_second_fat_only)
{
    int s[] = { BOOTVOL_EMPTY, BOOTVOL_BOOT };

    CHECK_EQ(bootvol_pick(s, 2), 1);
}

TEST(boot_files_on_both_fat)
{
    int s[] = { BOOTVOL_BOOT, BOOTVOL_BOOT };

    CHECK_EQ(bootvol_pick(s, 2), 0);
}

TEST(no_boot_files_keeps_first_mounted)
{
    int s[] = { BOOTVOL_NO_MOUNT, BOOTVOL_EMPTY, BOOTVOL_EMPTY };

    CHECK_EQ(bootvol_pick(s, 3), 1);
}

TEST(nothing_mounts)
{
    int s[] = { BOOTVOL_NO_MOUNT, BOOTVOL_NOT_FAT, BOOTVOL_NO_MOUNT };

    CHECK_EQ(bootvol_pick(s, 3), -1);
}

/* Extended and GPT protective entries are not FAT: never probed, never picked. */
TEST(extended_and_gpt_ignored)
{
    int s[] = { BOOTVOL_NOT_FAT, BOOTVOL_NOT_FAT, BOOTVOL_NOT_FAT, BOOTVOL_BOOT };
    int none[] = { BOOTVOL_NOT_FAT };

    CHECK_EQ(bootvol_pick(s, 4), 3);
    CHECK_EQ(bootvol_pick(none, 1), -1);
}
