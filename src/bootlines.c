#include "bootlines.h"

#include <robco/board.h>
#include <robco/klibc.h>

void bootlines_title(char *buf, size_t size, const char *version)
{
    k_snprintf(buf, size, "RETROS BIOS %s", version);
}

/*
 * Lore text, fixed. The version string and the copyright years recreate
 * the games' terminal boot screen. They describe nothing real.
 */
static const char *const lore[] = {
    "RBIOS-4.02.08.00 52EE5.E7.E8",
    "Copyright 2201-2203 Robco Ind.",
};

const char *bootlines_lore(unsigned i)
{
    return i < sizeof(lore) / sizeof(lore[0]) ? lore[i] : NULL;
}

void bootlines_memory(char *buf, size_t size, uint32_t mem_bytes)
{
    k_snprintf(buf, size, "Uppermem: %u KB", (unsigned)(mem_bytes / 1024u));
}

void bootlines_board(char *buf, size_t size, uint32_t board_rev, const char *soc_name)
{
    k_snprintf(buf, size, "Board: %s, %s, rev 0x%08x", board_model_name(board_rev),
               soc_name != NULL ? soc_name : "unknown", (unsigned)board_rev);
}

const char *bootlines_post_title(void)
{
    return "POWER-ON SELF TEST";
}

void bootlines_post_row(char *buf, size_t size, unsigned probe, const post_item_t *item)
{
    const char *res;

    switch (item->result) {
    case POST_OK:     res = "OK"; break;
    case POST_FAIL:   res = "FAILED"; break;
    case POST_ABSENT: res = "ABSENT"; break;
    case POST_SKIPPED: res = "SKIPPED"; break;
    default:          res = "?"; break;
    }
    k_snprintf(buf, size, "  %-8s %-7s%s", post_label(probe), res, item->detail);
}

void bootlines_post_summary(char *buf, size_t size, const post_counts_t *c)
{
    if (c->skipped != 0) {
        k_snprintf(buf, size, "POST: %u OK, %u FAILED, %u ABSENT, %u SKIPPED", c->ok, c->fail,
                   c->absent, c->skipped);
    } else {
        k_snprintf(buf, size, "POST: %u OK, %u FAILED, %u ABSENT", c->ok, c->fail, c->absent);
    }
}

const char *bootlines_key_prompt(void)
{
    return "Press any key for the emergency shell";
}

const char *bootlines_memtest_warning(void)
{
    return "WARNING: memory test failed. Results may be wrong.";
}

const char *bootlines_halted(void)
{
    return "BOOT HALTED. Maintenance port is listening.";
}

void bootlines_settings(char *buf, size_t size, uint32_t delay_ms, int beep,
                        const char *scheme, const char *source)
{
    k_snprintf(buf, size, "Settings: delay %u ms, beep %s, scheme %s (%s)", (unsigned)delay_ms,
               beep ? "on" : "off", scheme, source);
}

void bootlines_loading(char *buf, size_t size, const char *path)
{
    k_snprintf(buf, size, "Loading %s", path);
}

void bootlines_loaded(char *buf, size_t size, const char *name, uint32_t bytes, uint32_t crc)
{
    k_snprintf(buf, size, "Loaded %s: %u bytes, CRC 0x%08x", name, (unsigned)bytes,
               (unsigned)crc);
}

void bootlines_start(char *buf, size_t size, const char *name, uint32_t entry)
{
    k_snprintf(buf, size, "Starting %s at 0x%08x", name, (unsigned)entry);
}
