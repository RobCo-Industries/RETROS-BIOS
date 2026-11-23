/*
 * The boot volume around fs_mount_boot(): the failure of each PART_BOOT_*
 * code, and the release of an earlier volume. The volume rule itself is
 * RobCo-Core's, tested there.
 */

#include <stdio.h>
#include <string.h>

#include "bootvol.h"
#include "rc_test.h"

static char buf[48];

static part_info_t entry(uint8_t index, uint64_t start, uint64_t count)
{
    part_info_t p;

    memset(&p, 0, sizeof(p));
    p.index = index;
    p.type = 0x0e;
    p.start_lba = start;
    p.block_count = count;
    return p;
}

TEST(is_same_slot_start_and_size)
{
    fs_boot_t vol;
    part_info_t p = entry(0, 2048, 30720);

    memset(&vol, 0, sizeof(vol));
    vol.slot = 0;
    vol.start_lba = 2048;
    vol.block_count = 30720;
    CHECK(bootvol_is(&vol, &p));
}

TEST(is_not_same_slot_on_another_card)
{
    fs_boot_t vol;
    part_info_t moved = entry(0, 4096, 28672);
    part_info_t shorter = entry(0, 2048, 8192);
    part_info_t other = entry(1, 2048, 30720);

    memset(&vol, 0, sizeof(vol));
    vol.slot = 0;
    vol.start_lba = 2048;
    vol.block_count = 30720;
    CHECK(!bootvol_is(&vol, &moved));
    CHECK(!bootvol_is(&vol, &shorter));
    CHECK(!bootvol_is(&vol, &other));
}

TEST(is_whole_card)
{
    fs_boot_t vol;
    part_info_t whole = entry(0, 0, 32768);
    part_info_t slot0 = entry(0, 0, 32768);

    whole.superfloppy = 1;
    memset(&vol, 0, sizeof(vol));
    vol.slot = FS_BOOT_WHOLE;
    vol.block_count = 32768;
    CHECK(bootvol_is(&vol, &whole));
    CHECK(!bootvol_is(&vol, &slot0));
    whole.block_count = 65536;
    CHECK(!bootvol_is(&vol, &whole));
}

TEST(fail_no_partition)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_DEVICE, -5, 0, buf, sizeof(buf)), "part") == 0);
    CHECK(strcmp(buf, "no partition") == 0);
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_NOFAT, 0, 1, buf, sizeof(buf)), "part") == 0);
    CHECK(strcmp(buf, "no partition") == 0);
}

TEST(fail_no_fat)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_NOFAT, FS_ERR_NOFS, 0, buf, sizeof(buf)), "nofat") == 0);
    CHECK(strcmp(buf, "no FAT partition") == 0);
}

TEST(fail_gpt)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_GPT, 0, 0, buf, sizeof(buf)), "gpt") == 0);
    CHECK(strcmp(buf, "GPT card, not supported") == 0);
}

TEST(fail_mount)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_MOUNT, FS_ERR_NOFS, 0, buf, sizeof(buf)), "mount") == 0);
    CHECK(strcmp(buf, "no FAT volume mounts") == 0);
}

/* A probed volume that did not unmount: the name stays taken, as for an old volume. */
TEST(fail_release)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_RELEASE, FS_ERR_NOTREADY, 0, buf, sizeof(buf)),
                 "held") == 0);
    snprintf(buf + 40, 8, "%d", FS_ERR_NOTREADY);
    CHECK(strncmp(buf, "volume does not unmount, error ", 31) == 0);
    CHECK(strcmp(buf + 31, buf + 40) == 0);
}

/* Codes the probe never gets: the library's name. */
TEST(fail_other)
{
    CHECK(strcmp(bootvol_fail(PART_BOOT_ERR_BUSY, 0, 0, buf, sizeof(buf)), "busy") == 0);
    CHECK(strcmp(buf, "boot volume error busy") == 0);
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
