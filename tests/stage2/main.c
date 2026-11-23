/*
 * Test second stage. RETROS-BIOS loads it as /ROBCO/MFBOOT.SYS. It checks
 * what it receives against BOOT_PROTOCOL.md in RobCo-Core and prints one
 * marker per check:
 *
 *   @RC stage=stage2 ev=check name=<check> result=pass|fail [detail]
 *
 * then "ev=bootinfo" with the field values, "ev=checks passed=<n>
 * failed=<n>" and "ev=ready".
 */

#include <stdarg.h>
#include <stdint.h>

#include <robco/board.h>
#include <robco/boot.h>
#include <robco/bootinfo.h>
#include <robco/crc32.h>
#include <robco/klibc.h>
#include <robco/rimg.h>
#include <robco/uart.h>

#define STAGE "stage2"
#define SLOT_BASE 0x00200000u

static robco_bootinfo_t fresh;
static unsigned passed, failed;
static uint32_t image_crc, image_hdr_crc;

static void check(const char *name, int ok, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void check(const char *name, int ok, const char *fmt, ...)
{
    char detail[128];
    va_list ap;

    va_start(ap, fmt);
    k_vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    k_printf("@RC stage=" STAGE " ev=check name=%s result=%s%s%s\n", name, ok ? "pass" : "fail",
             detail[0] != '\0' ? " " : "", detail);
    if (ok) {
        passed++;
    } else {
        failed++;
    }
}

static uint32_t rd32(uint32_t addr)
{
    return *(volatile const uint32_t *)addr;
}

/*
 * CRC-32 of this image as loaded, the way tools/rimg.py seals it: the
 * header crc32 field counts as 0. start.S keeps the entry registers in
 * BSS, outside the file bytes, so nothing else differs from the file.
 * Runs first, before this file changes any initialised data.
 */
static void crc_own_image(void)
{
    const rimg_header_t *h = (const rimg_header_t *)(SLOT_BASE + RIMG_HDR_OFFSET);
    const uint8_t *img = (const uint8_t *)SLOT_BASE;
    uint32_t hole = RIMG_HDR_OFFSET + 0x1C;
    uint32_t c;

    image_hdr_crc = h->crc32;
    c = crc32_update(0, img, hole);
    c = crc32_update(c, "\0\0\0\0", 4);
    image_crc = crc32_update(c, img + hole + 4, h->file_size - hole - 4);
}

/* 1 if p is a NUL-terminated string in n bytes, with zeros after the NUL. */
static int clean_string(const char *p, size_t n)
{
    size_t i = 0;

    while (i < n && p[i] != '\0') {
        i++;
    }
    if (i == n) {
        return 0;
    }
    for (; i < n; i++) {
        if (p[i] != '\0') {
            return 0;
        }
    }
    return 1;
}

void robco_main(void)
{
    const robco_boot_args_t *a = robco_boot_args();
    const robco_board_t *b = board_get();
    const robco_bootinfo_t *bi = (const robco_bootinfo_t *)ROBCO_BOOTINFO_ADDR;
    uint32_t sctlr;
    uint32_t want_caps = ROBCO_CAP_FB | ROBCO_CAP_PL011 | ROBCO_CAP_MINIUART | ROBCO_CAP_SD;
    int v;

    crc_own_image();
    uart_init(115200);
    k_printf("\n@RC stage=" STAGE " ev=boot midr=0x%08x soc=%s base=0x%08x board_rev=0x%08x "
             "mem=0x%08x entry_mode=%s\n",
             (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base, (unsigned)b->board_rev,
             (unsigned)b->mem_size, a->entry_mode == 0x13 ? "svc" : "other");
    k_printf("@RC stage=" STAGE " ev=handoff trusted=%d\n", bootinfo_inherited());

    /* Registers at entry. */
    check("r0", a->r0 == 0, "value=0x%08x", (unsigned)a->r0);
    check("r1", a->r1 != 0 && a->r1 == bi->fw_r1, "value=0x%08x", (unsigned)a->r1);
    check("r2", a->r2 == ROBCO_HANDOFF_FDT_ADDR, "value=0x%08x", (unsigned)a->r2);
    check("r3", a->r3 == ROBCO_BOOTINFO_ADDR, "value=0x%08x", (unsigned)a->r3);
    check("mode", a->entry_mode == 0x13, "value=0x%02x", (unsigned)a->entry_mode);
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    check("sctlr", (sctlr & ((1u << 0) | (1u << 2) | (1u << 12))) == 0, "value=0x%08x",
          (unsigned)sctlr);

    /* The trust rule and the block's own CRC. */
    v = bootinfo_validate(bi);
    check("bootinfo_crc", v == BOOTINFO_OK, "rc=%d", v);
    check("trust", bootinfo_inherited() == 1 && bootinfo_get() != NULL, "inherited=%d",
          bootinfo_inherited());
    check("image_crc", image_crc == image_hdr_crc, "crc=0x%08x hdr=0x%08x", (unsigned)image_crc,
          (unsigned)image_hdr_crc);

    /* Fields this stage measures itself, for comparison. */
    bootinfo_build(&fresh, ROBCO_STAGE_BOOTAGENT);
    check("stage", bi->stage == ROBCO_STAGE_BIOS, "value=%u", (unsigned)bi->stage);
    check("board", bi->board_rev == b->board_rev && bi->soc == (uint32_t)b->soc &&
                   bi->periph_base == b->periph_base && bi->midr == b->midr,
          "rev=0x%08x soc=0x%x", (unsigned)bi->board_rev, (unsigned)bi->soc);
    check("memory", bi->mem_base == b->mem_base && bi->mem_size == b->mem_size && bi->mem_size != 0,
          "size=0x%08x", (unsigned)bi->mem_size);
    check("fdt", bi->fdt_addr == ROBCO_HANDOFF_FDT_ADDR && bi->fdt_size >= 8 &&
                 bi->fdt_size <= ROBCO_HANDOFF_FDT_MAX && rd32(bi->fdt_addr + 4) == ROBCO_ATAG_CORE,
          "addr=0x%08x size=%u", (unsigned)bi->fdt_addr, (unsigned)bi->fdt_size);
    check("caps", (bi->caps & want_caps) == want_caps && (bi->caps >> 5) == 0 &&
                  (bi->caps & ROBCO_CAP_LOCAL_INTC) == (fresh.caps & ROBCO_CAP_LOCAL_INTC),
          "value=0x%08x", (unsigned)bi->caps);
    check("uart", bi->uart_type == ROBCO_UART_PL011 && bi->uart_baud == 115200 &&
                  bi->uart_clock != 0,
          "type=%u baud=%u", (unsigned)bi->uart_type, (unsigned)bi->uart_baud);
    check("fb", bi->fb_addr != 0 && bi->fb_width == 640 && bi->fb_height == 480 &&
                bi->fb_depth == 32 && bi->fb_pitch >= 640 * 4 &&
                bi->fb_size >= bi->fb_pitch * 480,
          "addr=0x%08x", (unsigned)bi->fb_addr);
    check("console", bi->con_cols == 80 && bi->con_rows == 30 && bi->con_cy < bi->con_rows &&
                     bi->con_cx < bi->con_cols && bi->con_scheme <= 3,
          "cy=%u scheme=%u", (unsigned)bi->con_cy, (unsigned)bi->con_scheme);
    check("boot_dev", bi->boot_dev == ROBCO_BOOT_DEV_EMMC && bi->boot_part_lba != 0,
          "dev=%u lba=%u", (unsigned)bi->boot_dev, (unsigned)bi->boot_part_lba);
    check("boot_flags", (bi->boot_flags & ROBCO_BOOT_COLD) != 0 &&
                        (bi->boot_flags & ~(ROBCO_BOOT_COLD | ROBCO_BOOT_FALLBACK)) == 0,
          "value=0x%08x", (unsigned)bi->boot_flags);
    check("boot_path", clean_string(bi->boot_path, sizeof(bi->boot_path)) &&
                       k_strncmp(bi->boot_path, "/ROBCO/MFBOOT.", 14) == 0, "len=%u",
          (unsigned)k_strlen(bi->boot_path));
    check("cmdline", clean_string(bi->cmdline, sizeof(bi->cmdline)) && bi->cmdline[0] == '\0', "first=%u",
          (unsigned)(uint8_t)bi->cmdline[0]);

    k_printf("@RC stage=" STAGE " ev=bootinfo prev_stage=%u flags=0x%08x caps=0x%08x dev=%u "
             "part_lba=%u path=%s scheme=%u cy=%u fw_r1=0x%08x fb=0x%08x\n",
             (unsigned)bi->stage, (unsigned)bi->boot_flags, (unsigned)bi->caps,
             (unsigned)bi->boot_dev, (unsigned)bi->boot_part_lba,
             clean_string(bi->boot_path, sizeof(bi->boot_path)) && bi->boot_path[0] != '\0' ?
             bi->boot_path : "none",
             (unsigned)bi->con_scheme, (unsigned)bi->con_cy, (unsigned)bi->fw_r1,
             (unsigned)bi->fb_addr);
    k_printf("@RC stage=" STAGE " ev=checks passed=%u failed=%u\n", passed, failed);
    k_printf("@RC stage=" STAGE " ev=ready\n");
    uart_flush();
}
