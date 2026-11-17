/* pico-umac
 *
 * Main loop to initialise umac, and run main event loop (piping
 * keyboard/mouse events in).
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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hw.h"
#include "video.h"
#include "kbd.h"
#if SOUND
#include "snd.h"
#endif

#include "bsp/rp2040/board.h"
#include "tusb.h"

#include "umac.h"

#if USE_SD
#include "f_util.h"
#include "ff.h"
#include "rtc.h"
#include "hw_config.h"
#endif

#if PICO_ZERO
#include "ws2812.h"
#include "ws2812.pio.h"
#endif

#if TFT_2P
#include "tft_2p.h"
#endif

#if BEEP
#include "audio.h"
#endif

////////////////////////////////////////////////////////////////////////////////
// Imports and data

extern void     hid_app_task(void);
extern int cursor_x;
extern int cursor_y;
extern int cursor_button;

// Mac binary data:  disc and ROM images
static const uint8_t umac_disc[] = {
#include "umac-disc.h"
};
static const uint8_t umac_rom[] = {
#include "umac-rom.h"
};

static uint8_t umac_ram[RAM_SIZE];

////////////////////////////////////////////////////////////////////////////////


static void     io_init()
{
#if PICO_ZERO
                PIO pio = pio1; /* Use PIO 1 as Vidio is using PIO 0 */
                uint offset = pio_add_program(pio, &ws2812_program);
                uint sm = pio_claim_unused_sm(pio, true); //Claim a free state machine on a PIO instance
                ws2812_program_init(pio, sm, offset, GPIO_LED_PIN, 800000, true); /* 800000 is frequency */
#else
                gpio_init(GPIO_LED_PIN);
                gpio_set_dir(GPIO_LED_PIN, GPIO_OUT);
#endif
}

static void     poll_led_etc()
{
        static uint8_t led_on = 0;
        static absolute_time_t last = 0;
        absolute_time_t now = get_absolute_time();

        if (absolute_time_diff_us(last, now) > 500*1000) {
                last = now;

                led_on ^= 1;
#if PICO_ZERO
                put_pixel_green(led_on); 
#else
                gpio_put(GPIO_LED_PIN, led_on);
#endif
        }
}

static int umac_cursor_x = 0;
static int umac_cursor_y = 0;
static int umac_cursor_button = 0;

#if USE_SD
static void     write_log_file(const char *title);
#endif

static void     poll_umac()
{
        static absolute_time_t last_1hz = 0;
        static absolute_time_t last_vsync = 0;
        absolute_time_t now = get_absolute_time();

        umac_loop();

        int64_t p_1hz = absolute_time_diff_us(last_1hz, now);
        int64_t p_vsync = absolute_time_diff_us(last_vsync, now);
        if (p_vsync >= 16667) {
                /* FIXME: Trigger this off actual vsync */
#if SOUND
                snd_frame();
#endif
                umac_vsync_event();
                /* Advance by exactly one frame (rather than setting to
                 * 'now', which loses the loop's latency every frame and
                 * makes the Mac run slow); only re-sync if we're far behind.
                 */
                last_vsync = delayed_by_us(last_vsync, 16667);
                if (absolute_time_diff_us(last_vsync, now) > 100000)
                        last_vsync = now;
        }
        if (p_1hz >= 1000000) {
                umac_1hz_event();
                last_1hz = now;
#if USE_SD
                /* Second snapshot of the log, once the keyboard etc. have settled */
                static int secs = 0;
                if (++secs == 15)
                        write_log_file("pico-mac-nano log, 15 s after start\n");
#endif
        }

        int update = 0;
        int dx = 0;
        int dy = 0;
        int b = umac_cursor_button;
        if (cursor_x != umac_cursor_x) {
                dx = cursor_x - umac_cursor_x;
                umac_cursor_x = cursor_x;
                update = 1;
        }
        if (cursor_y != umac_cursor_y) {
                dy = cursor_y - umac_cursor_y;
                umac_cursor_y = cursor_y;
                update = 1;
        }
        if (cursor_button != umac_cursor_button) {
                b = cursor_button;
                umac_cursor_button = cursor_button;
                update = 1;
        }
        if (update) {
                umac_mouse(dx, -dy, b);
        }

        if (!kbd_queue_empty() && umac_kbd_space() >= 3) {
                uint16_t k = kbd_queue_pop();
                bool down = !!(k & 0x8000);
                uint8_t code = (k & 0xff) | (down ? 0 : 0x80);
                switch (KBD_PAD_KIND(k)) {
                case 1: {
                        uint8_t seq[2] = { 0x79, code };
                        umac_kbd_put(seq, 2);
                        break;
                }
                case 2: {
                        uint8_t seq[3] = { down ? 0x71 : 0xf1, 0x79, code };
                        umac_kbd_put(seq, 3);
                        break;
                }
                default:
                        umac_kbd_event(k & 0xff, down);
                }
        }
}

#if USE_SD
static int      disc_do_read(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FIL *fp = (FIL *)ctx;
        f_lseek(fp, offset);
        unsigned int did_read = 0;
        FRESULT fr = f_read(fp, data, len, &did_read);
        if (fr != FR_OK || len != did_read) {
                printf("disc: f_read returned %d, read %u (of %u)\n", fr, did_read, len);
                return -1;
        }
        return 0;
}

static int      disc_do_write(void *ctx, uint8_t *data, unsigned int offset, unsigned int len)
{
        FIL *fp = (FIL *)ctx;
        f_lseek(fp, offset);
        unsigned int did_write = 0;
        FRESULT fr = f_write(fp, data, len, &did_write);
        if (fr != FR_OK || len != did_write) {
                printf("disc: f_write returned %d, read %u (of %u)\n", fr, did_write, len);
                return -1;
        }
        return 0;
}

static FIL discfp[DISC_NUM_DRIVES];
#endif

#if USE_SD
/* Which disc image to boot: umac<N>*.img, where N is a number key held
 * down while powering on (a USB keyboard takes a moment to enumerate, so
 * wait for it, but not for long if there isn't one).  No key means 0.
 */
#ifndef SD_SELECT_WAIT_MS
#define SD_SELECT_WAIT_MS       3000    /* Max wait for a keyboard; 0 disables selection */
#endif

static int      boot_image_number(void)
{
        if (SD_SELECT_WAIT_MS <= 0)
                return 0;

        absolute_time_t t0 = get_absolute_time();
        absolute_time_t end = delayed_by_ms(t0, SD_SELECT_WAIT_MS);
        while (!hid_kbd_seen && !time_reached(end))
                sleep_ms(5);
        int seen_ms = (int)(absolute_time_diff_us(t0, get_absolute_time()) / 1000);
        if (hid_kbd_seen)
                sleep_ms(100);  /* Let a held key's report arrive */

        int n = hid_boot_digit;
        if (hid_kbd_seen) {
                printf("  Keyboard found after %d ms, boot key: %d\n", seen_ms, n);
                hid_log("select: keyboard reported at %d ms, boot key %d", seen_ms, n);
        } else {
                printf("  No keyboard found\n");
                hid_log("select: no keyboard report within %d ms (keyboard enumerated: %s)",
                        seen_ms, hid_kbd_mounted ? "yes" : "no");
        }

        /* Keys pressed while waiting were meant for us, not the Mac: */
        while (!kbd_queue_empty())
                kbd_queue_pop();

        return n < 0 ? 0 : n;
}

/* Save the event log (what the USB keyboard did, and which images were
 * chosen) to picolog.txt on the SD card, since the only USB port is busy.
 */
static void     write_log_file(const char *title)
{
        FIL f;
        if (f_open(&f, "picolog.txt", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
                return;
        UINT w;
        f_write(&f, title, strlen(title), &w);
        unsigned int len;
        const char *log = hid_log_get(&len);
        f_write(&f, log, len, &w);
        f_close(&f);
}

/* Find and open <prefix><n>*.img (<prefix><n>ro.img is read-only) as
 * drive 'slot'.  Returns true on success.
 */
static bool     open_image(const char *prefix, int n, int slot, disc_descr_t discs[DISC_NUM_DRIVES], bool quiet)
{
        char ro_name[24], pattern[24];
        snprintf(ro_name, sizeof(ro_name), "%s%dro.img", prefix, n);
        snprintf(pattern, sizeof(pattern), "%s%d*.img", prefix, n);

        DIR di = {0};
        FILINFO fi = {0};
        FRESULT fr = f_findfirst(&di, &fi, "/", pattern);
        if (fr != FR_OK || !fi.fname[0]) {
                if (!quiet)
                        printf("  Can't find images %s: %s (%d)\n", pattern, FRESULT_str(fr), fr);
                f_closedir(&di);
                return false;
        }
        f_closedir(&di);

        int read_only = !strcmp(fi.fname, ro_name);
        printf("  Opening %s as drive %d (R%c)\n", fi.fname, slot + 1, read_only ? 'O' : 'W');

        /* Open image, set up disc info: */
        FIL *fp = &discfp[slot];
        fr = f_open(fp, fi.fname, FA_OPEN_EXISTING | FA_READ | FA_WRITE);
        if (fr != FR_OK && fr != FR_EXIST) {
                printf("  *** Can't open %s: %s (%d)!\n", fi.fname, FRESULT_str(fr), fr);
                return false;
        }
        printf("  Opened, size 0x%x\n", f_size(fp));
        if (read_only)
                printf("  (disc is read-only)\n");
        discs[slot].base = 0; // Means use R/W ops
        discs[slot].read_only = read_only;
        discs[slot].size = f_size(fp);
        discs[slot].op_ctx = fp;
        discs[slot].op_read = disc_do_read;
        discs[slot].op_write = disc_do_write;
        return true;
}

/* Extra volumes: <SD_EXTRA_PREFIX>1*.img, 2*.img, ... are mounted as
 * further drives (appearing on the desktop after the Mac has booted).
 */
#ifndef SD_EXTRA_PREFIX
#define SD_EXTRA_PREFIX         "vol"
#endif

static void     open_extra_volumes(disc_descr_t discs[DISC_NUM_DRIVES])
{
        for (int slot = 1; slot < DISC_NUM_DRIVES; slot++)
                open_image(SD_EXTRA_PREFIX, slot, slot, discs, true);
}
#endif

static void     use_flash_disc(disc_descr_t discs[DISC_NUM_DRIVES])
{
        discs[0].base = (void *)umac_disc;
        discs[0].read_only = 1;
        discs[0].size = sizeof(umac_disc);
}

static void     disc_setup(disc_descr_t discs[DISC_NUM_DRIVES])
{
#if USE_SD
        /* Mount SD filesystem */
        printf("Starting SPI/FatFS:\n");
        set_spi_dma_irq_channel(true, false);
        sd_card_t *pSD = sd_get_by_num(0);
        FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
        printf("  mount: %d\n", fr);
        if (fr != FR_OK) {
                printf("  error mounting disc: %s (%d)\n", FRESULT_str(fr), fr);
                goto no_sd;
        }

        /* Look for a disc image: the one selected at power-on, else umac0 */
        int n = boot_image_number();
        bool found = open_image("umac", n, 0, discs, false);
        if (!found && n != 0) {
                printf("  Falling back to umac0\n");
                found = open_image("umac", 0, 0, discs, false);
        }
        if (!found)
                use_flash_disc(discs);
        open_extra_volumes(discs);
        write_log_file("pico-mac-nano log, at boot\n");
        return;

        /* FIXME: Other files can be stored on SD too, such as logging
         * and NVRAM storage.
         */

no_sd:
#endif
        /* If we don't find (or look for) an SD-based image, attempt
         * to use in-flash disc image:
         */
        use_flash_disc(discs);
}

static void     core1_main()
{
        disc_descr_t discs[DISC_NUM_DRIVES] = {0};

        printf("Core 1 started\n");
        disc_setup(discs);

        umac_init(umac_ram, (void *)umac_rom, discs);

       /* Video runs on core 1, i.e. IRQs/DMA are unaffected by
         * core 0's USB activity.
         */
        video_init((uint32_t *)(umac_ram + umac_get_fb_offset()));

#if SOUND
        snd_init();
#endif

        printf("Enjoyable Mac times now begin:\n\n");

        while (true) {
                poll_umac();
        }
}

int     main()
{
       set_sys_clock_khz(250*1000, true);

	stdio_init_all();
        io_init(); /* Just sets up the LED */

#if BEEP
        printf("Output audio beep\n");
        beep();
#endif

#if TFT_2P /* Need to initialise TFT before UMAC starts because SPI is shared between TFT and SD card */
        printf("TFT init\n");
        tft_init();
#endif

        multicore_launch_core1(core1_main);

	printf("Starting, init usb\n");
        tusb_init();

        /* This happens on core 0: */
	while (true) {
                tuh_task();
                hid_app_task();
                poll_led_etc();
	}

	return 0;
}

