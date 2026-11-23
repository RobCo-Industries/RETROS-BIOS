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

static fs_stat_t stat_of(uint32_t size, int dir)
{
    fs_stat_t st;

    memset(&st, 0, sizeof(st));
    st.size = size;
    st.is_dir = (uint8_t)dir;
    st.attr = dir ? FS_ATTR_DIR : 0;
    return st;
}

/* "Holds the boot files" means a regular file of at least one byte. */
TEST(boot_file_is_regular_non_empty)
{
    fs_stat_t st = stat_of(4096, 0);

    CHECK(bootvol_is_boot_file(&st));
    st = stat_of(1, 0);
    CHECK(bootvol_is_boot_file(&st));
}

TEST(folder_is_not_boot_file)
{
    fs_stat_t st = stat_of(0, 1);

    CHECK(!bootvol_is_boot_file(&st));
    st.attr = 0; /* is_dir alone */
    CHECK(!bootvol_is_boot_file(&st));
    st = stat_of(0, 0);
    st.attr = FS_ATTR_DIR; /* the attribute alone */
    CHECK(!bootvol_is_boot_file(&st));
}

TEST(empty_file_is_not_boot_file)
{
    fs_stat_t st = stat_of(0, 0);

    CHECK(!bootvol_is_boot_file(&st));
}

/* A fake fs_unmount(): returns fake_rc, counts the calls. */
static int fake_rc;
static int fake_calls;

static int fake_unmount(void)
{
    fake_calls++;
    return fake_rc;
}

TEST(release_with_nothing_mounted)
{
    int mounted = 0;

    fake_calls = 0;
    fake_rc = FS_ERR_IO;
    CHECK_EQ(bootvol_release(&mounted, fake_unmount), 0);
    CHECK_EQ(mounted, 0);
    CHECK_EQ(fake_calls, 0);
}

/*
 * The card swap story: the card that booted is pulled, POST runs, then a
 * good card goes in and POST runs again.
 */
TEST(release_keeps_volume_while_card_is_gone)
{
    int mounted = 1;

    fake_calls = 0;
    /* No card: the sync before the unmount fails, the volume stays registered. */
    fake_rc = FS_ERR_NOTREADY;
    CHECK_EQ(bootvol_release(&mounted, fake_unmount), FS_ERR_NOTREADY);
    CHECK_EQ(mounted, 1);
    CHECK_EQ(fake_calls, 1);
    /* POST again, still no card: same answer, nothing forgotten. */
    CHECK_EQ(bootvol_release(&mounted, fake_unmount), FS_ERR_NOTREADY);
    CHECK_EQ(mounted, 1);
    /* A card answers again: the unmount succeeds and the name is free. */
    fake_rc = 0;
    CHECK_EQ(bootvol_release(&mounted, fake_unmount), 0);
    CHECK_EQ(mounted, 0);
    CHECK_EQ(fake_calls, 3);
    /* Nothing left to release. */
    CHECK_EQ(bootvol_release(&mounted, fake_unmount), 0);
    CHECK_EQ(fake_calls, 3);
}
