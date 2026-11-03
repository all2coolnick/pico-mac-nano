/* Control the Mac from a PC, over a USB serial port.  See usb_pc.h.
 */

#include <stdbool.h>
#include <string.h>
#include "hardware/timer.h"
#include "pico/bootrom.h"
#include "tusb.h"

#include "kbd.h"
#include "usb_pc.h"

#define BYTES_PER_LINE          (DISP_WIDTH / 8)
#define FRAME_US                33333   /* Look for screen changes at about 30Hz */
#define LINES_PER_TASK          8       /* Keep servicing USB between lines */

#define PKT_SIZE                0x5a
#define PKT_LINE                0xa5

#define CMD_MOUSE_POS           0x01
#define CMD_MOUSE_BUTTON        0x02
#define CMD_KEY                 0x03
#define CMD_REFRESH             0x04
#define CMD_BOOTLOADER          0x05

#define MOUSE_POS_NONE          0xffffffff

/* Mac low memory globals for the pointer position */
#define MAC_MTEMP               0x828
#define MAC_RAWMOUSE            0x82c
#define MAC_CRSRNEW             0x8ce
#define MAC_CRSRCOUPLE          0x8cf

/* Used by main.c, in place of the ones in hid.c.  The pointer position is set by usb_pc_poll() instead. */
int cursor_x = 0;
int cursor_y = 0;
int cursor_button = 0;

static uint8_t *mac_ram;
static const uint8_t *mac_fb;
/* What the PC last received, so only changed lines are sent */
static uint8_t sent_fb[DISP_HEIGHT][BYTES_PER_LINE];

static volatile uint32_t mouse_pos = MOUSE_POS_NONE;

static bool was_connected = false;
static bool send_size = false;
static bool send_all_next = false;
static bool send_all = false;
static bool scanning = false;
static unsigned int scan_y = 0;
static uint32_t scan_start_us = 0;

static uint8_t cmd[5];
static unsigned int cmd_len = 0;

void    usb_pc_init(uint8_t *ram, unsigned int fb_offset)
{
        mac_ram = ram;
        mac_fb = ram + fb_offset;
}

static void     mac_write16(unsigned int addr, uint16_t val)
{
        mac_ram[addr] = val >> 8;
        mac_ram[addr + 1] = val & 0xff;
}

/* The Mac accelerates mouse movement, so a PC's pointer position can't be
 * reached by sending deltas.  Set the position directly instead.
 */
void    usb_pc_poll(void)
{
        static uint32_t last = MOUSE_POS_NONE;
        uint32_t pos = mouse_pos;

        if (pos != last) {
                last = pos;
                mac_write16(MAC_MTEMP, pos & 0xffff);
                mac_write16(MAC_MTEMP + 2, pos >> 16);
                mac_write16(MAC_RAWMOUSE, pos & 0xffff);
                mac_write16(MAC_RAWMOUSE + 2, pos >> 16);
                mac_ram[MAC_CRSRNEW] = mac_ram[MAC_CRSRCOUPLE];
        }
}

static unsigned int     cmd_length(uint8_t op)
{
        switch (op) {
        case CMD_MOUSE_POS:     return 5;
        case CMD_MOUSE_BUTTON:  return 2;
        case CMD_KEY:           return 3;
        case CMD_REFRESH:       return 1;
        case CMD_BOOTLOADER:    return 5;
        default:                return 0;
        }
}

static void     request_refresh(void)
{
        send_size = true;
        send_all_next = true;
}

static void     run_cmd(void)
{
        switch (cmd[0]) {
        case CMD_MOUSE_POS:
                mouse_pos = ((uint32_t)((cmd[1] << 8) | cmd[2]) << 16) | ((cmd[3] << 8) | cmd[4]);
                break;
        case CMD_MOUSE_BUTTON:
                cursor_button = !!cmd[1];
                break;
        case CMD_KEY:
                kbd_queue_push(cmd[1], !!cmd[2]);
                break;
        case CMD_REFRESH:
                request_refresh();
                break;
        case CMD_BOOTLOADER:
                if (!memcmp(&cmd[1], "BOOT", 4))
                        reset_usb_boot(0, 0);
                break;
        }
}

static void     poll_rx(void)
{
        while (tud_cdc_available()) {
                /* Leave keys in the USB buffer rather than drop one, which could
                 * lose a release and leave the key held down.
                 */
                if ((cmd_len > 0) && (cmd[0] == CMD_KEY) && kbd_queue_full())
                        return;

                uint8_t c = tud_cdc_read_char();
                if ((cmd_len == 0) && (cmd_length(c) == 0))
                        continue;
                cmd[cmd_len++] = c;
                if (cmd_len == cmd_length(cmd[0])) {
                        run_cmd();
                        cmd_len = 0;
                }
        }
}

static void     poll_tx(void)
{
        if (send_size) {
                if (tud_cdc_write_available() < 5)
                        return;
                const uint8_t pkt[5] = { PKT_SIZE, DISP_WIDTH >> 8, DISP_WIDTH & 0xff,
                                         DISP_HEIGHT >> 8, DISP_HEIGHT & 0xff };
                tud_cdc_write(pkt, sizeof(pkt));
                send_size = false;
        }

        if (!scanning) {
                uint32_t now = time_us_32();
                if ((now - scan_start_us) < FRAME_US)
                        return;
                scan_start_us = now;
                scanning = true;
                scan_y = 0;
                send_all = send_all_next;
                send_all_next = false;
        }

        for (unsigned int i = 0; i < LINES_PER_TASK; i++) {
                uint8_t pkt[2 + BYTES_PER_LINE];

                if (tud_cdc_write_available() < sizeof(pkt)) {
                        tud_cdc_write_flush();
                        return;
                }
                /* Copy first: the emulator is writing to this on the other core */
                memcpy(&pkt[2], mac_fb + (scan_y * BYTES_PER_LINE), BYTES_PER_LINE);
                if (send_all || memcmp(&pkt[2], sent_fb[scan_y], BYTES_PER_LINE)) {
                        memcpy(sent_fb[scan_y], &pkt[2], BYTES_PER_LINE);
                        pkt[0] = PKT_LINE;
                        pkt[1] = scan_y;
                        tud_cdc_write(pkt, sizeof(pkt));
                }
                if (++scan_y >= DISP_HEIGHT) {
                        scanning = false;
                        tud_cdc_write_flush();
                        return;
                }
        }
}

void    usb_pc_task(void)
{
        if (!tud_cdc_connected()) {
                was_connected = false;
                return;
        }
        if (!was_connected) {
                was_connected = true;
                cmd_len = 0;
                request_refresh();
        }
        poll_rx();
        poll_tx();
}
