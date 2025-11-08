#ifndef SDCARD_H
#define SDCARD_H

#include <stdint.h>

// SD card initialization
int sd_init(void);

// Read a block from SD card
int sd_read_block(uint32_t block_num, uint8_t *buffer);

// Write a block to SD card (optional for chain-loading)
int sd_write_block(uint32_t block_num, const uint8_t *buffer);

#endif // SDCARD_H
