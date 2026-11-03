/* Control the Mac from a PC, over a USB serial port.  See tools/PicoMacViewer.
 *
 * PC to device:
 *   01 xh xl yh yl     Move the pointer to an absolute screen position
 *   02 b               Set the mouse button (0 or 1)
 *   03 k d             USB HID keycode k, pressed if d is non-zero
 *   04                 Resend the whole screen, preceded by the size packet
 *   05 'B' 'O' 'O' 'T' Reboot into the UF2 bootloader
 *
 * Device to PC:
 *   5A wh wl hh hl     Screen size, in pixels
 *   A5 y <DISP_WIDTH/8 bytes>
 *                      One changed screen line, MSB leftmost and 1 is black
 */
#ifndef USB_PC_H
#define USB_PC_H

#include <inttypes.h>

/* Passed the Mac's RAM and the offset of its framebuffer */
void    usb_pc_init(uint8_t *ram, unsigned int fb_offset);
/* Call from core 0 */
void    usb_pc_task(void);
/* Call from the core running the emulator, between umac_loop() calls */
void    usb_pc_poll(void);

#endif
