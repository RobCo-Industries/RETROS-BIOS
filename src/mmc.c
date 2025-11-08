#include "mmc.h"
#include "hardware.h"
#include "timer.h"
#include "gpio.h"

// EMMC registers (Broadcom EMMC controller)
#define EMMC_ARG2       (EMMC_BASE + 0x00)
#define EMMC_BLKSIZECNT (EMMC_BASE + 0x04)
#define EMMC_ARG1       (EMMC_BASE + 0x08)
#define EMMC_CMDTM      (EMMC_BASE + 0x0C)
#define EMMC_RESP0      (EMMC_BASE + 0x10)
#define EMMC_RESP1      (EMMC_BASE + 0x14)
#define EMMC_RESP2      (EMMC_BASE + 0x18)
#define EMMC_RESP3      (EMMC_BASE + 0x1C)
#define EMMC_DATA       (EMMC_BASE + 0x20)
#define EMMC_STATUS     (EMMC_BASE + 0x24)
#define EMMC_CONTROL0   (EMMC_BASE + 0x28)
#define EMMC_CONTROL1   (EMMC_BASE + 0x2C)
#define EMMC_INTERRUPT  (EMMC_BASE + 0x30)
#define EMMC_IRPT_MASK  (EMMC_BASE + 0x34)
#define EMMC_IRPT_EN    (EMMC_BASE + 0x38)
#define EMMC_CONTROL2   (EMMC_BASE + 0x3C)
#define EMMC_SLOTISR    (EMMC_BASE + 0xFC)

// EMMC commands
#define CMD_GO_IDLE_STATE       0
#define CMD_SEND_OP_COND        1
#define CMD_ALL_SEND_CID        2
#define CMD_SEND_RELATIVE_ADDR  3
#define CMD_SET_DSR             4
#define CMD_SWITCH_FUNC         6
#define CMD_SELECT_CARD         7
#define CMD_SEND_IF_COND        8
#define CMD_SEND_CSD            9
#define CMD_SEND_CID            10
#define CMD_VOLTAGE_SWITCH      11
#define CMD_STOP_TRANSMISSION   12
#define CMD_SEND_STATUS         13
#define CMD_SET_BLOCKLEN        16
#define CMD_READ_SINGLE_BLOCK   17
#define CMD_READ_MULTIPLE_BLOCK 18
#define CMD_WRITE_BLOCK         24
#define CMD_WRITE_MULTIPLE_BLOCK 25
#define CMD_APP_CMD             55

// App-specific commands
#define ACMD_SET_BUS_WIDTH      6
#define ACMD_SD_STATUS          13
#define ACMD_SEND_NUM_WR_BLOCKS 22
#define ACMD_SET_WR_BLK_ERASE_COUNT 23
#define ACMD_SD_SEND_OP_COND    41

// Status register bits
#define SR_READ_AVAILABLE       (1 << 11)
#define SR_WRITE_AVAILABLE      (1 << 10)
#define SR_DAT_INHIBIT          (1 << 1)
#define SR_CMD_INHIBIT          (1 << 0)

// Interrupt flags
#define INT_CMD_DONE            (1 << 0)
#define INT_DATA_DONE           (1 << 1)
#define INT_ERROR               (1 << 15)

static mmc_card_info_t card_info;
static int mmc_initialized = 0;

static void mmc_delay(uint32_t ms) {
    timer_wait_ms(ms);
}

static int mmc_wait_for_interrupt(uint32_t mask) {
    uint32_t timeout = 1000000;  // 1 second timeout

    while (timeout--) {
        uint32_t status = MMIO_READ(EMMC_INTERRUPT);
        if (status & INT_ERROR) {
            MMIO_WRITE(EMMC_INTERRUPT, 0xFFFFFFFF);
            return -1;
        }
        if (status & mask) {
            MMIO_WRITE(EMMC_INTERRUPT, mask);
            return 0;
        }
    }

    return -1;
}

static int mmc_send_command(uint32_t cmd, uint32_t arg) {
    // Wait for command inhibit to clear
    uint32_t timeout = 1000000;
    while ((MMIO_READ(EMMC_STATUS) & SR_CMD_INHIBIT) && timeout--) {
        asm volatile("nop");
    }

    if (timeout == 0) return -1;

    // Clear interrupts
    MMIO_WRITE(EMMC_INTERRUPT, 0xFFFFFFFF);

    // Send command
    MMIO_WRITE(EMMC_ARG1, arg);
    MMIO_WRITE(EMMC_CMDTM, cmd);

    // Wait for command complete
    return mmc_wait_for_interrupt(INT_CMD_DONE);
}

int mmc_init(void) {
    // Configure GPIO pins for EMMC (pins 48-53)
    // These are typically pre-configured by the bootloader

    // Reset controller
    MMIO_WRITE(EMMC_CONTROL1, 0);
    mmc_delay(10);

    // Set clock to 400 kHz for identification mode
    uint32_t c1 = MMIO_READ(EMMC_CONTROL1);
    c1 |= (0x3E << 8);  // Clock divider
    c1 |= (1 << 0);     // Internal clock enable
    MMIO_WRITE(EMMC_CONTROL1, c1);
    mmc_delay(10);

    // Enable clock
    c1 |= (1 << 2);  // Clock enable
    MMIO_WRITE(EMMC_CONTROL1, c1);
    mmc_delay(10);

    // Send CMD0 - GO_IDLE_STATE
    if (mmc_send_command(CMD_GO_IDLE_STATE, 0) != 0) {
        return -1;
    }

    // Send CMD8 - SEND_IF_COND (check voltage)
    if (mmc_send_command(CMD_SEND_IF_COND, 0x1AA) != 0) {
        // Might be SD v1 or MMC
        card_info.type = MMC_TYPE_SD1;
    } else {
        card_info.type = MMC_TYPE_SD2;
    }

    // Send ACMD41 to initialize card
    uint32_t retries = 1000;
    while (retries--) {
        // Send CMD55 (APP_CMD) first
        if (mmc_send_command(CMD_APP_CMD, 0) != 0) {
            return -1;
        }

        // Send ACMD41
        uint32_t arg = 0x00FF8000;  // Voltage range
        if (card_info.type == MMC_TYPE_SD2) {
            arg |= 0x40000000;  // HCS (High Capacity Support)
        }

        if (mmc_send_command(ACMD_SD_SEND_OP_COND, arg) == 0) {
            uint32_t resp = MMIO_READ(EMMC_RESP0);
            if (resp & 0x80000000) {
                // Card is ready
                if (resp & 0x40000000) {
                    card_info.type = MMC_TYPE_SDHC;
                }
                break;
            }
        }

        mmc_delay(10);
    }

    if (retries == 0) return -1;

    // Get CID
    if (mmc_send_command(CMD_ALL_SEND_CID, 0) != 0) {
        return -1;
    }

    // Get RCA
    if (mmc_send_command(CMD_SEND_RELATIVE_ADDR, 0) != 0) {
        return -1;
    }
    card_info.rca = MMIO_READ(EMMC_RESP0) >> 16;

    // Select card
    if (mmc_send_command(CMD_SELECT_CARD, card_info.rca << 16) != 0) {
        return -1;
    }

    // Set block size to 512 bytes
    if (mmc_send_command(CMD_SET_BLOCKLEN, 512) != 0) {
        return -1;
    }

    card_info.block_size = 512;
    mmc_initialized = 1;

    return 0;
}

mmc_card_info_t *mmc_get_card_info(void) {
    if (!mmc_initialized) return 0;
    return &card_info;
}

int mmc_read_blocks(uint32_t start_block, uint32_t num_blocks, uint8_t *buffer) {
    if (!mmc_initialized) return -1;

    // Set block count and size
    MMIO_WRITE(EMMC_BLKSIZECNT, (num_blocks << 16) | 512);

    // Send read command
    uint32_t cmd = (num_blocks == 1) ? CMD_READ_SINGLE_BLOCK : CMD_READ_MULTIPLE_BLOCK;
    if (mmc_send_command(cmd, start_block) != 0) {
        return -1;
    }

    // Read data
    for (uint32_t block = 0; block < num_blocks; block++) {
        for (uint32_t i = 0; i < 512; i += 4) {
            // Wait for data
            uint32_t timeout = 1000000;
            while (!(MMIO_READ(EMMC_STATUS) & SR_READ_AVAILABLE) && timeout--) {
                asm volatile("nop");
            }

            if (timeout == 0) return -1;

            uint32_t data = MMIO_READ(EMMC_DATA);
            buffer[block * 512 + i + 0] = (data >> 0) & 0xFF;
            buffer[block * 512 + i + 1] = (data >> 8) & 0xFF;
            buffer[block * 512 + i + 2] = (data >> 16) & 0xFF;
            buffer[block * 512 + i + 3] = (data >> 24) & 0xFF;
        }
    }

    // Stop transmission for multiple blocks
    if (num_blocks > 1) {
        mmc_send_command(CMD_STOP_TRANSMISSION, 0);
    }

    return 0;
}

int mmc_write_blocks(uint32_t start_block, uint32_t num_blocks, const uint8_t *buffer) {
    if (!mmc_initialized) return -1;

    // Set block count and size
    MMIO_WRITE(EMMC_BLKSIZECNT, (num_blocks << 16) | 512);

    // Send write command
    uint32_t cmd = (num_blocks == 1) ? CMD_WRITE_BLOCK : CMD_WRITE_MULTIPLE_BLOCK;
    if (mmc_send_command(cmd, start_block) != 0) {
        return -1;
    }

    // Write data
    for (uint32_t block = 0; block < num_blocks; block++) {
        for (uint32_t i = 0; i < 512; i += 4) {
            // Wait for buffer available
            uint32_t timeout = 1000000;
            while (!(MMIO_READ(EMMC_STATUS) & SR_WRITE_AVAILABLE) && timeout--) {
                asm volatile("nop");
            }

            if (timeout == 0) return -1;

            uint32_t data = buffer[block * 512 + i + 0] |
                           (buffer[block * 512 + i + 1] << 8) |
                           (buffer[block * 512 + i + 2] << 16) |
                           (buffer[block * 512 + i + 3] << 24);
            MMIO_WRITE(EMMC_DATA, data);
        }
    }

    // Wait for data done
    if (mmc_wait_for_interrupt(INT_DATA_DONE) != 0) {
        return -1;
    }

    // Stop transmission for multiple blocks
    if (num_blocks > 1) {
        mmc_send_command(CMD_STOP_TRANSMISSION, 0);
    }

    return 0;
}

void mmc_reset(void) {
    MMIO_WRITE(EMMC_CONTROL1, 0);
    mmc_initialized = 0;
}

int mmc_card_present(void) {
    // Check slot interrupt status register
    uint32_t status = MMIO_READ(EMMC_SLOTISR);
    return (status & 0xFFFF) != 0;
}
