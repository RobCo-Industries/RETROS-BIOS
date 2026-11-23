/*
 * Input backends and the maintenance port. The port is the mini UART
 * script channel: in QEMU it is the second serial port. On hardware the
 * mini UART reaches no pin, see BOARD_SUPPORT.md.
 *
 * Project commands, besides the library's built-in ones:
 *   state   one line: phase, error, shell_requested, post
 *   dump    the screen text, through console_dump()
 *   reboot  restarts the board through the watchdog
 */

#include <robco/audio.h>
#include <robco/console.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/power.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>

#include "bios.h"
#include "boot.h"

static input_script_reply_t *dump_reply;

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    dump_reply->write(s);
}

static const char *phase_name(int phase)
{
    switch (phase) {
    case BIOS_PHASE_INIT:   return "init";
    case BIOS_PHASE_POST:   return "post";
    case BIOS_PHASE_KEYS:   return "keys";
    case BIOS_PHASE_LOAD:   return "load";
    case BIOS_PHASE_HALTED: return "halted";
    default:                return "unknown";
    }
}

static int command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    char line[96];

    (void)ctx;
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        k_snprintf(line, sizeof(line), "phase=%s error=%s shell_requested=%d post=%s\n",
                   phase_name(bios.phase), bios.error != 0 ? bios_err_name(bios.error) : "none",
                   bios.shell_requested, bios.post_fatal != 0 ? "fail" : "ok");
        reply->write(line);
        return 0;
    }
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        if (!bios.fb_ok) {
            reply->reason = "no screen";
            return -1;
        }
        dump_reply = reply;
        console_dump(dump_char);
        return 0;
    }
    if (k_strcmp(argv[0], "reboot") == 0 && argc == 1) {
        BIOS_MARK("reboot");
        uart_flush();
        power_reboot();
    }
    reply->reason = "unknown command";
    return -1;
}

void maint_start(void)
{
    int mini = uart_mini_init(115200);
    int serial;
    int script = INPUT_ERR_NO_UART;

    input_init();
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(command, NULL);
    if (mini == 0) {
        script = input_script_start();
    }
    BIOS_MARK("input serial=%d mini=%d script=%d", serial, mini, script);
}

void maint_idle(void)
{
    input_event_t ev;

    bios.phase = BIOS_PHASE_HALTED;
    BIOS_MARK("idle");
    for (;;) {
        /* Console keys are read and dropped; script lines are answered. */
        (void)input_wait(&ev, 1000);
        audio_poll();
    }
}
