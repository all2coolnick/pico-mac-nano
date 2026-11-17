/*
 * pico-umac keyboard scancode mapping
 *
 * Copyright 2024 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef KBD_H
#define KBD_H

#include <inttypes.h>
#include <stdbool.h>

bool            kbd_queue_empty();
/* If empty, return 0, else return a mac keycode in [7:0] and [15] set if a press (else release) */
uint16_t        kbd_queue_pop();
/* kbd_queue_pop() value: [7:0] key code, [15] press (else release),
 * [14:13] 0 = plain key, 1 = send 0x79 prefix first, 2 = send 0x71, 0x79 first.
 */
#define KBD_PAD_KIND(v) (((v) >> 13) & 3)

/* FIXME: map modifiers */
bool            kbd_queue_push(uint8_t hid_keycode, bool pressed);

/* Boot-time image selection (see main.c disc_setup()).  hid.c sets
 * hid_kbd_seen once it has processed a report from a keyboard (including
 * the state read at enumeration time, so a key held while powering on is
 * seen), and hid_boot_digit to the most recent 0-9 key (top row or keypad)
 * seen pressed, or -1.
 */
extern volatile bool    hid_kbd_seen;
void hid_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *hid_log_get(unsigned int *len);
extern volatile bool    hid_kbd_mounted;        /* A keyboard interface has enumerated */
extern volatile int8_t  hid_boot_digit;

#endif
