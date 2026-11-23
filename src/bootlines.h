/*
 * Every line of text the BIOS shows on the screen and the console. Pure
 * code: it builds and runs on the host in the unit tests, so the wording
 * lives here and nowhere else.
 *
 * Lore lines are fixed text in the style of the games' terminal boot
 * screen. Every other line shows real values. Each function says which.
 */
#ifndef RETROS_BOOTLINES_H
#define RETROS_BOOTLINES_H

#include <stddef.h>
#include <stdint.h>

#include "post.h"

/* Real: "RETROS BIOS <version>". */
void bootlines_title(char *buf, size_t size, const char *version);

/* Lore: fixed header line i, or NULL past the last one. */
const char *bootlines_lore(unsigned i);

/* Real: "Uppermem: <n> KB", the ARM memory the mailbox reports. */
void bootlines_memory(char *buf, size_t size, uint32_t mem_bytes);

/* Real: the board model from the revision code, such as "Raspberry Pi 2 Model B". */
const char *bootlines_board_name(uint32_t board_rev);

/* Real: "Board: <model>, <soc>, rev 0x<rev>". */
void bootlines_board(char *buf, size_t size, uint32_t board_rev, const char *soc_name);

/* Fixed: the POST title line. */
const char *bootlines_post_title(void);

/* Real: one POST row, "  <LABEL>   <RESULT>  <detail>". */
void bootlines_post_row(char *buf, size_t size, unsigned probe, const post_item_t *item);

/* Real: "POST: <n> OK, <n> FAILED, <n> ABSENT", then ", <n> SKIPPED" when some probes did not run. */
void bootlines_post_summary(char *buf, size_t size, const post_counts_t *c);

/* Fixed prompts and notes. */
const char *bootlines_key_prompt(void);
const char *bootlines_shell_not_built(void);
const char *bootlines_halted(void);

/* Real: "Settings: delay <n> ms, beep on, scheme green (<source>)". */
void bootlines_settings(char *buf, size_t size, uint32_t delay_ms, int beep,
                        const char *scheme, const char *source);

/* Real: "Loading <path>". */
void bootlines_loading(char *buf, size_t size, const char *path);

/* Real: "Loaded <name>: <n> bytes, CRC 0x<crc>". */
void bootlines_loaded(char *buf, size_t size, const char *name, uint32_t bytes, uint32_t crc);

/* Real: "Starting <name> at 0x<entry>". */
void bootlines_start(char *buf, size_t size, const char *name, uint32_t entry);

#endif
