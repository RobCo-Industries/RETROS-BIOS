/* Boot screen text: every line, with real values at their limits. */

#include "bootlines.h"
#include "boot.h"
#include "rc_test.h"

TEST(title_has_version)
{
    char b[BIOS_LINE_MAX];

    bootlines_title(b, sizeof(b), "0.1.0");
    CHECK_STR(b, "RETROS BIOS 0.1.0");
}

TEST(lore_lines_fixed)
{
    CHECK_STR(bootlines_lore(0), "RBIOS-4.02.08.00 52EE5.E7.E8");
    CHECK_STR(bootlines_lore(1), "Copyright 2201-2203 Robco Ind.");
    CHECK(bootlines_lore(2) == NULL);
}

TEST(memory_line_in_kb)
{
    char b[BIOS_LINE_MAX];

    bootlines_memory(b, sizeof(b), 0x1c000000u);
    CHECK_STR(b, "Uppermem: 458752 KB");
    bootlines_memory(b, sizeof(b), 0xffffffffu);
    CHECK_STR(b, "Uppermem: 4194303 KB");
    bootlines_memory(b, sizeof(b), 0);
    CHECK_STR(b, "Uppermem: 0 KB");
}

TEST(board_names_of_qemu_boards)
{
    CHECK_STR(bootlines_board_name(0x00920092u), "Raspberry Pi Zero");
    CHECK_STR(bootlines_board_name(0x00900021u), "Raspberry Pi 1 Model A+");
    CHECK_STR(bootlines_board_name(0x00a21041u), "Raspberry Pi 2 Model B");
    CHECK_STR(bootlines_board_name(0x00a02082u), "Raspberry Pi 3 Model B");
    CHECK_STR(bootlines_board_name(0x00902120u), "Raspberry Pi Zero 2 W");
    CHECK_STR(bootlines_board_name(0x0000000eu), "Raspberry Pi 1");
    CHECK_STR(bootlines_board_name(0), "unknown board");
    CHECK_STR(bootlines_board_name(0x00800ff0u), "unknown board");
}

TEST(board_line_fits_screen_at_worst)
{
    char b[BIOS_LINE_MAX];

    bootlines_board(b, sizeof(b), 0x00a21041u, "bcm2836");
    CHECK_STR(b, "Board: Raspberry Pi 2 Model B, bcm2836, rev 0x00a21041");
    bootlines_board(b, sizeof(b), 0xffffffffu, NULL);
    CHECK_STR(b, "Board: unknown board, unknown, rev 0xffffffff");
    CHECK(strlen(b) < 80);
}

TEST(post_rows)
{
    char b[BIOS_LINE_MAX];
    post_item_t it = { POST_OK, "256 KiB at 0x00400000" };

    bootlines_post_row(b, sizeof(b), POST_MEMORY, &it);
    CHECK_STR(b, "  MEMORY   OK     256 KiB at 0x00400000");
    it.result = POST_FAIL;
    strcpy(it.detail, "no framebuffer");
    bootlines_post_row(b, sizeof(b), POST_VIDEO, &it);
    CHECK_STR(b, "  VIDEO    FAILED no framebuffer");
    it.result = POST_ABSENT;
    it.detail[0] = '\0';
    bootlines_post_row(b, sizeof(b), POST_SD, &it);
    CHECK_STR(b, "  SD CARD  ABSENT ");
    it.result = POST_SKIPPED;
    bootlines_post_row(b, sizeof(b), POST_AUDIO, &it);
    CHECK_STR(b, "  AUDIO    SKIPPED");
}

TEST(post_row_longest_detail_fits)
{
    char b[BIOS_LINE_MAX];
    post_item_t it;

    it.result = POST_FAIL;
    memset(it.detail, 'x', sizeof(it.detail) - 1);
    it.detail[sizeof(it.detail) - 1] = '\0';
    bootlines_post_row(b, sizeof(b), POST_AUDIO, &it);
    CHECK_EQ(strlen(b), 18 + sizeof(it.detail) - 1);
    CHECK(strlen(b) < 80);
}

TEST(post_summary_counts)
{
    char b[BIOS_LINE_MAX];
    post_counts_t c = { 7, 0, 1, 0 };

    bootlines_post_summary(b, sizeof(b), &c);
    CHECK_STR(b, "POST: 7 OK, 0 FAILED, 1 ABSENT");
    c.ok = c.fail = c.absent = 0xffffffffu;
    bootlines_post_summary(b, sizeof(b), &c);
    CHECK_STR(b, "POST: 4294967295 OK, 4294967295 FAILED, 4294967295 ABSENT");
    c.ok = 1;
    c.fail = 1;
    c.absent = 0;
    c.skipped = 6;
    bootlines_post_summary(b, sizeof(b), &c);
    CHECK_STR(b, "POST: 1 OK, 1 FAILED, 0 ABSENT, 6 SKIPPED");
    c.ok = c.fail = c.absent = c.skipped = 0xffffffffu;
    bootlines_post_summary(b, sizeof(b), &c);
    CHECK_STR(b, "POST: 4294967295 OK, 4294967295 FAILED, 4294967295 ABSENT, 4294967295 SKIPPED");
    CHECK(strlen(b) <= 79);
}

TEST(fixed_lines)
{
    CHECK_STR(bootlines_post_title(), "POWER-ON SELF TEST");
    CHECK_STR(bootlines_key_prompt(), "Press any key for the emergency shell");
    CHECK_STR(bootlines_memtest_warning(), "WARNING: memory test failed. Results may be wrong.");
    CHECK_STR(bootlines_halted(), "BOOT HALTED. Maintenance port is listening.");
}

TEST(settings_line)
{
    char b[BIOS_LINE_MAX];

    bootlines_settings(b, sizeof(b), 1000, 1, "green", "/ROBCO/BOOT.CFG");
    CHECK_STR(b, "Settings: delay 1000 ms, beep on, scheme green (/ROBCO/BOOT.CFG)");
    bootlines_settings(b, sizeof(b), 0, 0, "amber", "defaults");
    CHECK_STR(b, "Settings: delay 0 ms, beep off, scheme amber (defaults)");
}

TEST(load_lines)
{
    char b[BIOS_LINE_MAX];

    bootlines_loading(b, sizeof(b), BIOS_PRIMARY);
    CHECK_STR(b, "Loading /ROBCO/MFBOOT.SYS");
    bootlines_loaded(b, sizeof(b), "mfboot", 81920, 0x1c291ca3u);
    CHECK_STR(b, "Loaded mfboot: 81920 bytes, CRC 0x1c291ca3");
    bootlines_loaded(b, sizeof(b), "0123456789abcdef", 0xffffffffu, 0xffffffffu);
    CHECK_STR(b, "Loaded 0123456789abcdef: 4294967295 bytes, CRC 0xffffffff");
    bootlines_start(b, sizeof(b), "mfboot", 0x00200000u);
    CHECK_STR(b, "Starting mfboot at 0x00200000");
}

TEST(lines_cut_to_buffer)
{
    char b[8];

    bootlines_title(b, sizeof(b), "0.1.0");
    CHECK_STR(b, "RETROS ");
}
