#ifndef MMC_H
#define MMC_H

#include <stdint.h>

// MMC/SD card types
typedef enum {
    MMC_TYPE_UNKNOWN = 0,
    MMC_TYPE_MMC = 1,
    MMC_TYPE_SD1 = 2,
    MMC_TYPE_SD2 = 3,
    MMC_TYPE_SDHC = 4
} mmc_card_type_t;

// MMC card information
typedef struct {
    mmc_card_type_t type;
    uint32_t rca;               // Relative card address
    uint32_t ocr;               // Operating conditions register
    uint32_t capacity;          // Card capacity in blocks
    uint32_t block_size;        // Block size in bytes
    uint8_t csd[16];            // Card-specific data
    uint8_t cid[16];            // Card identification
} mmc_card_info_t;

// Initialize MMC/SD card controller
int mmc_init(void);

// Get card information
mmc_card_info_t *mmc_get_card_info(void);

// Read blocks from card
int mmc_read_blocks(uint32_t start_block, uint32_t num_blocks, uint8_t *buffer);

// Write blocks to card
int mmc_write_blocks(uint32_t start_block, uint32_t num_blocks, const uint8_t *buffer);

// Reset MMC controller
void mmc_reset(void);

// Check if card is present
int mmc_card_present(void);

#endif // MMC_H
