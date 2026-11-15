/*
 * USB HID host input for pico-mac-nano (keyboards and mice).
 *
 * Derived from pico-examples/usb/host/host_cdc_msc_hid/hid_app.c, which is
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
 * Further changes are Copyright 2024 Matt Evans
 *
 * Rewritten to be driven by each device's HID report descriptor, rather
 * than assuming the fixed "boot protocol" report layouts.  Many mice
 * (anything with 12/16-bit X/Y fields, report IDs, extra buttons) and
 * keyboards (N-key rollover, report IDs) do not use the boot layouts,
 * which previously gave wrong or missing movement and keypresses.
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "tusb.h"

#include "kbd.h"

/*
 * Mouse speed, as a percentage.  100 = one HID count moves the Mac
 * pointer by one step.  If your mouse is high-DPI and the pointer is too
 * fast, try a smaller number (e.g. 50).  Fractions are carried over, so
 * slow movement is not lost.
 */
#ifndef MOUSE_SCALE_PERCENT
#define MOUSE_SCALE_PERCENT     100
#endif

//--------------------------------------------------------------------+
// Exported to main.c (consumed from the other core)
//--------------------------------------------------------------------+

/* Exported for use by other thread! */
int cursor_x = 0;
int cursor_y = 0;
int cursor_button = 0;

//--------------------------------------------------------------------+
// Per-interface description of where things live in the input reports
//--------------------------------------------------------------------+

#define MAX_FIELDS      12      /* Interesting fields kept per HID interface */
#define MAX_USAGES      16      /* Usages remembered per descriptor main item */
#define MAX_REPORT_IDS  8       /* Distinct report IDs tracked while parsing */
#define PUSH_DEPTH      4       /* HID Push/Pop global state depth */
#define MAX_ELEMENTS    256     /* Sanity cap on elements examined per item */

enum {
        F_BUTTON1,      /* Primary mouse button (1 bit, usually) */
        F_X,            /* Relative mouse X */
        F_Y,            /* Relative mouse Y */
        F_KEYBITS,      /* Run of 1-bit key flags: modifiers, or NKRO bitmap */
        F_KEYARRAY,     /* Array of key codes (boot-style 6 keys) */
};

typedef struct {
        uint8_t  kind;
        uint8_t  report_id;     /* 0 if the descriptor uses no report IDs */
        uint8_t  size;          /* Bits per element */
        bool     is_signed;
        uint16_t bit_off;       /* From first bit after the report ID byte */
        uint16_t count;         /* Number of elements */
        uint16_t base;          /* F_KEYBITS: key code of the first bit */
} hid_field_t;

typedef struct {
        bool        active;     /* We understand this interface & are listening */
        bool        has_ids;    /* Reports start with a report ID byte */
        uint8_t     n_fields;
        hid_field_t fields[MAX_FIELDS];
        uint8_t     keys_down[32];  /* Bitmap of HID key codes currently held */
        bool        button;
} hid_itf_t;

static hid_itf_t hid_itf[CFG_TUH_HID];

//--------------------------------------------------------------------+
// Report descriptor parser
//--------------------------------------------------------------------+

enum { APP_OTHER, APP_MOUSE, APP_KEYBOARD };

typedef struct {
        uint16_t usage_page;
        int32_t  logical_min;
        uint32_t report_size;
        uint32_t report_count;
        uint8_t  report_id;
} globals_t;

typedef struct {
        uint32_t usages[MAX_USAGES];    /* Upper 16 bits = explicit page, or 0 */
        uint8_t  n_usages;
        uint32_t usage_min;
        uint32_t usage_max;
        bool     have_min;
        bool     have_max;
} locals_t;

typedef struct {
        hid_itf_t *h;
        globals_t  g;
        locals_t   l;
        globals_t  stack[PUSH_DEPTH];
        uint8_t    sp;
        uint8_t    app;
        uint8_t    depth;
        uint8_t    npos;
        bool       bad;
        struct {
                uint8_t  id;
                uint32_t pos;
        } rpos[MAX_REPORT_IDS];         /* Running input bit position per ID */
} parser_t;

static int32_t item_value(const uint8_t *d, uint8_t size, bool is_signed)
{
        uint32_t v = 0;
        for (uint8_t i = 0; i < size; i++)
                v |= (uint32_t)d[i] << (8 * i);
        if (is_signed && size && size < 4 && (v & (1u << (8 * size - 1))))
                v |= ~0u << (8 * size);
        return (int32_t)v;
}

/* Usage (and its page) for element i of the current main item */
static uint16_t usage_of(const parser_t *p, uint32_t i, uint16_t *page)
{
        const locals_t *l = &p->l;
        uint32_t u = 0;

        if (l->n_usages) {
                u = l->usages[i < l->n_usages ? i : (uint32_t)(l->n_usages - 1)];
        } else if (l->have_min) {
                u = l->usage_min + i;
                if (l->have_max && u > l->usage_max)
                        u = l->usage_max;
        }
        *page = (u >> 16) ? (uint16_t)(u >> 16) : p->g.usage_page;
        return (uint16_t)(u & 0xffff);
}

static void add_field(hid_itf_t *h, uint8_t kind, uint8_t rid, uint32_t off,
                      uint8_t size, bool is_signed, uint16_t count, uint16_t base)
{
        /* Join adjacent key flags into one field (a 100-key bitmap = 1 field) */
        if (kind == F_KEYBITS && h->n_fields) {
                hid_field_t *q = &h->fields[h->n_fields - 1];
                if (q->kind == F_KEYBITS && q->report_id == rid &&
                    (uint32_t)q->bit_off + q->count == off &&
                    (uint32_t)q->base + q->count == base) {
                        q->count += count;
                        return;
                }
        }
        if (h->n_fields >= MAX_FIELDS)
                return;
        hid_field_t *f = &h->fields[h->n_fields++];
        f->kind = kind;
        f->report_id = rid;
        f->size = size;
        f->is_signed = is_signed;
        f->bit_off = (uint16_t)off;
        f->count = count;
        f->base = base;
}

static uint32_t *report_pos(parser_t *p, uint8_t id)
{
        for (uint8_t i = 0; i < p->npos; i++)
                if (p->rpos[i].id == id)
                        return &p->rpos[i].pos;
        if (p->npos >= MAX_REPORT_IDS)
                return NULL;
        p->rpos[p->npos].id = id;
        p->rpos[p->npos].pos = 0;
        return &p->rpos[p->npos++].pos;
}

/* An Input main item: advance the bit position, and note fields we use */
static void parse_input(parser_t *p, uint32_t flags)
{
        const globals_t *g = &p->g;
        hid_itf_t *h = p->h;
        uint32_t size = g->report_size;
        uint32_t count = g->report_count;

        if (size > 32 || count > 4096) {
                p->bad = true;
                return;
        }
        uint32_t *pos = report_pos(p, g->report_id);
        if (!pos) {
                p->bad = true;
                return;
        }
        uint32_t off = *pos;
        *pos = off + size * count;
        if (*pos > 0xf000) {
                p->bad = true;
                return;
        }

        if ((flags & 1) || size == 0)   /* Constant: padding */
                return;

        bool variable = !!(flags & 2);
        bool relative = !!(flags & 4);
        uint32_t n = count < MAX_ELEMENTS ? count : MAX_ELEMENTS;

        if (p->app == APP_MOUSE && variable) {
                for (uint32_t i = 0; i < n; i++) {
                        uint16_t page;
                        uint16_t u = usage_of(p, i, &page);
                        uint32_t eo = off + i * size;
                        if (page == 0x09 && u == 1) {
                                add_field(h, F_BUTTON1, g->report_id, eo, size, false, 1, 0);
                        } else if (page == 0x01 && relative && u == 0x30) {
                                add_field(h, F_X, g->report_id, eo, size, g->logical_min < 0, 1, 0);
                        } else if (page == 0x01 && relative && u == 0x31) {
                                add_field(h, F_Y, g->report_id, eo, size, g->logical_min < 0, 1, 0);
                        }
                }
        } else if (p->app == APP_KEYBOARD) {
                if (variable) {
                        if (size != 1)
                                return;
                        for (uint32_t i = 0; i < n; i++) {
                                uint16_t page;
                                uint16_t u = usage_of(p, i, &page);
                                if (page == 0x07 && u < 256)
                                        add_field(h, F_KEYBITS, g->report_id, off + i, 1, false, 1, u);
                        }
                } else {
                        uint16_t page;
                        usage_of(p, 0, &page);
                        if (page == 0x07)
                                add_field(h, F_KEYARRAY, g->report_id, off, (uint8_t)size, false, (uint16_t)count, 0);
                }
        }
}

static void parse_descriptor(hid_itf_t *h, const uint8_t *d, uint16_t len)
{
        parser_t p;
        memset(&p, 0, sizeof(p));
        p.h = h;

        uint16_t i = 0;
        while (i < len && !p.bad) {
                uint8_t b = d[i++];

                if (b == 0xfe) {        /* Long item: skip it */
                        if (i >= len)
                                break;
                        i += 2 + d[i];
                        continue;
                }
                uint8_t size = b & 3;
                if (size == 3)
                        size = 4;
                uint8_t type = (b >> 2) & 3;
                uint8_t tag = b >> 4;
                if (i + size > len)
                        break;
                const uint8_t *data = &d[i];
                i += size;
                uint32_t uval = (uint32_t)item_value(data, size, false);

                if (type == 1) {                /* Global */
                        switch (tag) {
                        case 0: p.g.usage_page = (uint16_t)uval; break;
                        case 1: p.g.logical_min = item_value(data, size, true); break;
                        case 7: p.g.report_size = uval; break;
                        case 8: p.g.report_id = (uint8_t)uval; h->has_ids = true; break;
                        case 9: p.g.report_count = uval; break;
                        case 10: if (p.sp < PUSH_DEPTH) p.stack[p.sp++] = p.g; break;
                        case 11: if (p.sp) p.g = p.stack[--p.sp]; break;
                        default: break;
                        }
                } else if (type == 2) {         /* Local */
                        switch (tag) {
                        case 0:
                                if (p.l.n_usages < MAX_USAGES)
                                        p.l.usages[p.l.n_usages++] = uval;
                                break;
                        case 1: p.l.usage_min = uval; p.l.have_min = true; break;
                        case 2: p.l.usage_max = uval; p.l.have_max = true; break;
                        default: break;
                        }
                } else if (type == 0) {         /* Main */
                        switch (tag) {
                        case 0x8:
                                parse_input(&p, uval);
                                break;
                        case 0xa:               /* Collection */
                                if (p.depth == 0) {
                                        uint16_t page;
                                        uint16_t u = usage_of(&p, 0, &page);
                                        p.app = APP_OTHER;
                                        if (page == 0x01 && (u == 0x02 || u == 0x01))
                                                p.app = APP_MOUSE;
                                        else if (page == 0x01 && (u == 0x06 || u == 0x07))
                                                p.app = APP_KEYBOARD;
                                }
                                if (p.depth < 255)
                                        p.depth++;
                                break;
                        case 0xc:               /* End collection */
                                if (p.depth)
                                        p.depth--;
                                if (!p.depth)
                                        p.app = APP_OTHER;
                                break;
                        default:                /* Output / Feature: no input bits */
                                break;
                        }
                        memset(&p.l, 0, sizeof(p.l));   /* Locals die after a main item */
                }
        }
}

/* Does this interface have what we need to be a mouse and/or keyboard? */
static bool itf_usable(const hid_itf_t *h)
{
        bool x = false, y = false, k = false;
        for (uint8_t i = 0; i < h->n_fields; i++) {
                switch (h->fields[i].kind) {
                case F_X: x = true; break;
                case F_Y: y = true; break;
                case F_KEYBITS:
                case F_KEYARRAY: k = true; break;
                default: break;
                }
        }
        return (x && y) || k;
}

/* When there's no usable report descriptor, assume the standard boot layouts */
static void use_boot_layout(hid_itf_t *h, uint8_t itf_protocol)
{
        h->has_ids = false;
        h->n_fields = 0;
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
                add_field(h, F_KEYBITS, 0, 0, 1, false, 8, 0xe0);
                add_field(h, F_KEYARRAY, 0, 16, 8, false, 6, 0);
        } else if (itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
                add_field(h, F_BUTTON1, 0, 0, 1, false, 1, 0);
                add_field(h, F_X, 0, 8, 8, true, 1, 0);
                add_field(h, F_Y, 0, 16, 8, true, 1, 0);
        }
}

//--------------------------------------------------------------------+
// Pulling values out of reports
//--------------------------------------------------------------------+

/* Extract an unaligned bit-field of up to 32 bits.  Returns false if it
 * doesn't lie within the report (never reads out of bounds).
 */
static bool get_bits(const uint8_t *r, uint16_t len, uint32_t bit_off,
                     uint8_t size, bool is_signed, int32_t *out)
{
        if (size == 0 || size > 32 || bit_off + size > (uint32_t)len * 8)
                return false;

        uint32_t v = 0;
        uint8_t done = 0;
        while (done < size) {
                uint32_t bit = bit_off + done;
                uint8_t shift = bit & 7;
                uint8_t avail = 8 - shift;
                uint8_t take = (size - done) < avail ? (size - done) : avail;
                uint32_t part = ((uint32_t)r[bit >> 3] >> shift) & ((1u << take) - 1);
                v |= part << done;
                done += take;
        }
        if (is_signed && size < 32 && (v & (1u << (size - 1))))
                v |= ~0u << size;
        *out = (int32_t)v;
        return true;
}

//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

static void update_cursor_button(void)
{
        int b = 0;
        for (int i = 0; i < CFG_TUH_HID; i++)
                if (hid_itf[i].active && hid_itf[i].button)
                        b = 1;
        cursor_button = b;
}

static void mouse_move(int dx, int dy)
{
        static int rem_x = 0;
        static int rem_y = 0;

        rem_x += dx * MOUSE_SCALE_PERCENT;
        rem_y += dy * MOUSE_SCALE_PERCENT;
        cursor_x += rem_x / 100;
        rem_x %= 100;
        cursor_y += rem_y / 100;
        rem_y %= 100;
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

#define KEY_MOD_FIRST   0xe0    /* HID codes 0xe0-0xe7 are Ctrl/Shift/Alt/GUI */
#define KEY_MOD_LAST    0xe7

static inline bool bit_test(const uint8_t *map, unsigned int code)
{
        return !!(map[code >> 3] & (1u << (code & 7)));
}

/* Turn the difference between held-key sets into press/release events.
 * Modifiers go first when pressed and last when released, so e.g. Shift+A
 * arriving in a single report is delivered as Shift, A (not A, Shift).
 */
static void kbd_apply(hid_itf_t *h, const uint8_t now[32])
{
        const uint8_t *was = h->keys_down;

        for (unsigned int c = KEY_MOD_FIRST; c <= KEY_MOD_LAST; c++)
                if (bit_test(now, c) && !bit_test(was, c))
                        kbd_queue_push(c, true);
        for (unsigned int c = 1; c < KEY_MOD_FIRST; c++) {
                bool n = bit_test(now, c);
                if (n != bit_test(was, c))
                        kbd_queue_push(c, n);
        }
        for (unsigned int c = KEY_MOD_FIRST; c <= KEY_MOD_LAST; c++)
                if (!bit_test(now, c) && bit_test(was, c))
                        kbd_queue_push(c, false);

        memcpy(h->keys_down, now, 32);
}

static void release_all(hid_itf_t *h)
{
        uint8_t none[32] = { 0 };
        kbd_apply(h, none);
        h->button = false;
}

//--------------------------------------------------------------------+
// Report handling
//--------------------------------------------------------------------+

static void process_report(hid_itf_t *h, const uint8_t *r, uint16_t len)
{
        uint8_t rid = 0;

        if (h->has_ids) {
                if (len < 1)
                        return;
                rid = r[0];
                r++;
                len--;
        }

        bool have_btn = false, have_move = false, have_kbd = false, kbd_ok = true;
        bool btn = false;
        int dx = 0, dy = 0;
        uint8_t now[32];
        memset(now, 0, sizeof(now));

        for (uint8_t fi = 0; fi < h->n_fields; fi++) {
                const hid_field_t *f = &h->fields[fi];
                int32_t v;

                if (f->report_id != rid)
                        continue;

                switch (f->kind) {
                case F_BUTTON1:
                        if (get_bits(r, len, f->bit_off, f->size, false, &v)) {
                                btn = (v != 0);
                                have_btn = true;
                        }
                        break;
                case F_X:
                        if (get_bits(r, len, f->bit_off, f->size, f->is_signed, &v)) {
                                dx += v;
                                have_move = true;
                        }
                        break;
                case F_Y:
                        if (get_bits(r, len, f->bit_off, f->size, f->is_signed, &v)) {
                                dy += v;
                                have_move = true;
                        }
                        break;
                case F_KEYBITS:
                        have_kbd = true;
                        if ((uint32_t)f->bit_off + f->count > (uint32_t)len * 8) {
                                kbd_ok = false;         /* Truncated: don't guess */
                                break;
                        }
                        for (uint16_t i = 0; i < f->count; i++) {
                                uint32_t bit = f->bit_off + i;
                                unsigned int code = f->base + i;
                                if (code < 256 && ((r[bit >> 3] >> (bit & 7)) & 1))
                                        now[code >> 3] |= 1u << (code & 7);
                        }
                        break;
                case F_KEYARRAY:
                        have_kbd = true;
                        for (uint16_t i = 0; i < f->count; i++) {
                                if (!get_bits(r, len, f->bit_off + (uint32_t)i * f->size, f->size, false, &v)) {
                                        kbd_ok = false;
                                        break;
                                }
                                if (v == 1) {           /* ErrorRollOver: too many keys; ignore report */
                                        kbd_ok = false;
                                        break;
                                }
                                if (v >= 4 && v < 256)
                                        now[v >> 3] |= 1u << (v & 7);
                        }
                        break;
                default:
                        break;
                }
        }

        if (have_btn) {
                h->button = btn;
                update_cursor_button();
        }
        if (have_move)
                mouse_move(dx, dy);
        if (have_kbd && kbd_ok)
                kbd_apply(h, now);
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void hid_app_task(void)
{
        static bool done = false;

        if (!done) {
                /* Keep devices in "report" protocol (the HID default) rather than
                 * asking for the boot protocol.  Many devices ignore or botch the
                 * boot protocol request, but all describe their reports properly.
                 * This must be set before the first device finishes enumerating;
                 * that takes hundreds of ms, and we get here within microseconds
                 * of tusb_init().
                 */
                tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
                done = true;
        }
}

// Invoked when device with hid interface is mounted
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it is skipped
// and desc_report = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
        if (instance >= CFG_TUH_HID)
                return;

        uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

        printf("HID device address = %d, instance = %d is mounted (protocol %d, descriptor %d bytes)\r\n",
               dev_addr, instance, itf_protocol, desc_len);

        hid_itf_t *h = &hid_itf[instance];
        memset(h, 0, sizeof(*h));

        if (desc_report && desc_len)
                parse_descriptor(h, desc_report, desc_len);

        if (!itf_usable(h)) {
                /* Descriptor missing, too big, or unrecognised.  If the interface
                 * claims to be a keyboard/mouse, fall back to the boot layout.
                 */
                use_boot_layout(h, itf_protocol);
        }

        if (!itf_usable(h)) {
                printf("  Not a keyboard or mouse we can use; ignoring\r\n");
                memset(h, 0, sizeof(*h));
                return;
        }

        printf("  Using %d report fields%s\r\n", h->n_fields, h->has_ids ? " (with report IDs)" : "");
        h->active = true;

        if (!tuh_hid_receive_report(dev_addr, instance))
                printf("Error: cannot request to receive report\r\n");
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
        printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);

        if (instance >= CFG_TUH_HID)
                return;

        hid_itf_t *h = &hid_itf[instance];
        if (h->active) {
                release_all(h);         /* Don't leave keys/buttons stuck down */
                memset(h, 0, sizeof(*h));
                update_cursor_button();
        }
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
        if (instance >= CFG_TUH_HID || !hid_itf[instance].active)
                return;

        process_report(&hid_itf[instance], report, len);

        // continue to request to receive report
        if (!tuh_hid_receive_report(dev_addr, instance))
                printf("Error: cannot request to receive report\r\n");
}
