/* Loader results to error codes and screen lines, and the boot fields of the boot info. */

#include <robco/bootinfo.h>
#include <robco/rimg.h>

#include "boot.h"
#include "rc_test.h"

TEST(codes_match_loader)
{
    CHECK_EQ(BIOS_ERR_NOFILE, RIMG_E_NOFILE);
    CHECK_EQ(BIOS_ERR_IO, RIMG_E_IO);
    CHECK_EQ(BIOS_ERR_MAGIC, RIMG_E_MAGIC);
    CHECK_EQ(BIOS_ERR_VERSION, RIMG_E_VERSION);
    CHECK_EQ(BIOS_ERR_SIZE, RIMG_E_SIZE);
    CHECK_EQ(BIOS_ERR_BOUNDS, RIMG_E_BOUNDS);
    CHECK_EQ(BIOS_ERR_ABI, RIMG_E_ABI);
    CHECK_EQ(BIOS_ERR_CRC, RIMG_E_CRC);
}

TEST(names_match_rimg_strerror)
{
    int c;

    for (c = RIMG_E_NOFILE; c <= RIMG_E_CRC; c++) {
        CHECK_STR(bios_err_name(c), rimg_strerror(c));
    }
    CHECK_STR(bios_err_name(BIOS_ERR_MEMTEST), "memtest");
    CHECK_STR(bios_err_name(BIOS_ERR_TIMER), "timer");
    CHECK_STR(bios_err_name(0), "unknown");
    CHECK_STR(bios_err_name(-5), "unknown");
}

TEST(error_lines)
{
    char b[BIOS_LINE_MAX];

    bios_error_line(b, sizeof(b), RIMG_E_CRC, BIOS_PRIMARY);
    CHECK_STR(b, "ERROR E08 CRC: /ROBCO/MFBOOT.SYS: image CRC does not match");
    bios_error_line(b, sizeof(b), RIMG_E_NOFILE, BIOS_BACKUP);
    CHECK_STR(b, "ERROR E01 NOFILE: /ROBCO/MFBOOT.BAK: file not found");
    bios_error_line(b, sizeof(b), RIMG_E_IO, BIOS_PRIMARY);
    CHECK_STR(b, "ERROR E02 IO: /ROBCO/MFBOOT.SYS: SD card or FAT volume not usable");
    bios_error_line(b, sizeof(b), RIMG_E_BOUNDS, BIOS_PRIMARY);
    CHECK_STR(b, "ERROR E06 BOUNDS: /ROBCO/MFBOOT.SYS: image does not fit its slot");
    bios_error_line(b, sizeof(b), BIOS_ERR_MEMTEST, NULL);
    CHECK_STR(b, "ERROR E16 MEMTEST: memory test failed");
    bios_error_line(b, sizeof(b), BIOS_ERR_TIMER, NULL);
    CHECK_STR(b, "ERROR E17 TIMER: system timer does not advance");
    bios_error_line(b, sizeof(b), 12345, "");
    CHECK_STR(b, "ERROR E99 UNKNOWN: unknown error");
}

TEST(every_error_line_fits_80_columns)
{
    char b[BIOS_LINE_MAX];
    char path[64];
    int c;

    memset(path, 'P', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    for (c = 0; c <= BIOS_ERR_TIMER; c++) {
        bios_error_line(b, sizeof(b), c, BIOS_PRIMARY);
        CHECK(strlen(b) <= 79);
        bios_error_line(b, sizeof(b), c, path);
        CHECK(strlen(b) <= 79);
    }
}

static void build(robco_bootinfo_t *bi)
{
    memset(bi, 0, sizeof(*bi));
    bi->magic = ROBCO_BOOTINFO_MAGIC;
    bi->version = ROBCO_BOOTINFO_VERSION;
    bi->size = ROBCO_BOOTINFO_SIZE;
    bi->stage = ROBCO_STAGE_BIOS;
    bi->boot_flags = ROBCO_BOOT_COLD;
    bi->caps = ROBCO_CAP_FB | ROBCO_CAP_SD;
    memcpy(bi->cmdline, "stale", 5);
    bootinfo_seal(bi);
}

TEST(set_boot_primary)
{
    robco_bootinfo_t bi;

    build(&bi);
    bios_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 2048, 0, BIOS_PRIMARY);
    CHECK_EQ(bootinfo_validate(&bi), BOOTINFO_OK);
    CHECK_EQ(bi.boot_dev, ROBCO_BOOT_DEV_EMMC);
    CHECK_EQ(bi.boot_part_lba, 2048);
    CHECK_EQ(bi.boot_flags, ROBCO_BOOT_COLD);
    CHECK_STR(bi.boot_path, BIOS_PRIMARY);
    CHECK_EQ(bi.cmdline[0], 0);
    CHECK_EQ(bi.caps, ROBCO_CAP_FB | ROBCO_CAP_SD);
    CHECK_EQ(bi.stage, ROBCO_STAGE_BIOS);
}

TEST(set_boot_fallback)
{
    robco_bootinfo_t bi;
    size_t i;

    build(&bi);
    memset(bi.boot_path, 'x', sizeof(bi.boot_path));
    bios_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 8192, 1, BIOS_BACKUP);
    CHECK_EQ(bootinfo_validate(&bi), BOOTINFO_OK);
    CHECK_EQ(bi.boot_flags, ROBCO_BOOT_COLD | ROBCO_BOOT_FALLBACK);
    CHECK_STR(bi.boot_path, BIOS_BACKUP);
    for (i = strlen(BIOS_BACKUP); i < sizeof(bi.boot_path); i++) {
        CHECK_EQ(bi.boot_path[i], 0);
    }
    /* A second call without the fallback clears the flag again. */
    bios_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 8192, 0, BIOS_PRIMARY);
    CHECK_EQ(bi.boot_flags, ROBCO_BOOT_COLD);
}

TEST(set_boot_long_path_cut)
{
    robco_bootinfo_t bi;
    char path[100];

    build(&bi);
    memset(path, 'a', sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    bios_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 1, 0, path);
    CHECK_EQ(strlen(bi.boot_path), sizeof(bi.boot_path) - 1);
    CHECK_EQ(bootinfo_validate(&bi), BOOTINFO_OK);
    bios_bootinfo_set_boot(&bi, ROBCO_BOOT_DEV_EMMC, 1, 0, NULL);
    CHECK_EQ(bi.boot_path[0], 0);
}
