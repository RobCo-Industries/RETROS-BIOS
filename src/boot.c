#include "boot.h"

#include <robco/klibc.h>

typedef struct {
    int code;
    const char *name;
    const char *text;
} err_info_t;

/* Names match rimg_strerror() and IMAGE_FORMAT.md. */
static const err_info_t errors[] = {
    { BIOS_ERR_NOFILE,  "nofile",  "file not found" },
    { BIOS_ERR_IO,      "io",      "SD card or FAT volume not usable" },
    { BIOS_ERR_MAGIC,   "magic",   "not a boot image" },
    { BIOS_ERR_VERSION, "version", "unknown header version" },
    { BIOS_ERR_SIZE,    "size",    "size does not match the file" },
    { BIOS_ERR_BOUNDS,  "bounds",  "image does not fit its slot" },
    { BIOS_ERR_ABI,     "abi",     "wrong ABI version" },
    { BIOS_ERR_CRC,     "crc",     "image CRC does not match" },
    { BIOS_ERR_MEMTEST, "memtest", "memory test failed" },
};

static const err_info_t *find(int code)
{
    size_t i;

    for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
        if (errors[i].code == code) {
            return &errors[i];
        }
    }
    return NULL;
}

const char *bios_err_name(int code)
{
    const err_info_t *e = find(code);

    return e != NULL ? e->name : "unknown";
}

const char *bios_err_text(int code)
{
    const err_info_t *e = find(code);

    return e != NULL ? e->text : "unknown error";
}

void bios_error_line(char *buf, size_t size, int code, const char *path)
{
    char upper[8];
    const char *name = bios_err_name(code);
    size_t i;
    /* Codes print as two digits. */
    unsigned shown = code >= 0 && code <= 99 ? (unsigned)code : 99u;

    for (i = 0; i + 1 < sizeof(upper) && name[i] != '\0'; i++) {
        upper[i] = (name[i] >= 'a' && name[i] <= 'z') ? (char)(name[i] - 'a' + 'A') : name[i];
    }
    upper[i] = '\0';
    if (path != NULL && path[0] != '\0') {
        k_snprintf(buf, size, "ERROR E%02u %s: %s: %s", shown, upper, path, bios_err_text(code));
    } else {
        k_snprintf(buf, size, "ERROR E%02u %s: %s", shown, upper, bios_err_text(code));
    }
}

void bios_bootinfo_set_boot(robco_bootinfo_t *bi, uint32_t boot_dev, uint32_t part_lba,
                            int fallback, const char *path)
{
    size_t n = path != NULL ? k_strlen(path) : 0;

    if (n > sizeof(bi->boot_path) - 1) {
        n = sizeof(bi->boot_path) - 1;
    }
    bi->boot_dev = boot_dev;
    bi->boot_part_lba = part_lba;
    if (fallback) {
        bi->boot_flags |= ROBCO_BOOT_FALLBACK;
    } else {
        bi->boot_flags &= ~ROBCO_BOOT_FALLBACK;
    }
    k_memset(bi->boot_path, 0, sizeof(bi->boot_path));
    if (n > 0) {
        k_memcpy(bi->boot_path, path, n);
    }
    k_memset(bi->cmdline, 0, sizeof(bi->cmdline));
    bootinfo_seal(bi);
}
