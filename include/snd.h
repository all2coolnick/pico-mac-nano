/* Mac sound output for pico-mac-nano.
 *
 * The Mac's 8-bit/22kHz sound buffer (see umac_audio_get_frame()) is
 * converted to a full-swing square wave on the two speaker pins
 * (GPIO_AUDIO and GPIO_AUDIO_INV), which must be the A and B outputs of one
 * PWM slice (e.g. GPIO10/11 or GPIO12/13).  A small speaker can be driven
 * directly between the two pins, as the startup beep already does.
 *
 * snd_init() is called once from core 1; snd_frame() once per emulated vsync.
 *
 * The pure signal-processing helper (snd_process) lives in this header,
 * with no Pico SDK dependency, so it can be unit-tested on a PC.
 *
 * Copyright 2024 Matt Evans (pico-mac); audio additions by the pico-mac-nano project.
 * Same MIT-style licence as the rest of pico-mac.
 */

#ifndef SND_H
#define SND_H

#include <stdint.h>
#include <stdbool.h>

#define SND_FRAME_SAMPLES       370     /* == UMAC_AUDIO_SAMPLES */
#define SND_HANG_FRAMES         8       /* Constant frames before going idle */
#define SND_DC_SHIFT            7       /* DC blocker: fc ~= 28Hz at 22kHz */

#ifndef SND_VOL_FULL_MIN
#define SND_VOL_FULL_MIN        5       /* Mac volume >= this: full swing */
#endif
#define SND_HYST                4       /* Slicer hysteresis (8-bit units) */

/* Output codes (index into the lut passed to snd_process()): */
#define SND_CODE_LOW            0       /* Both pins low */
#define SND_CODE_P              1       /* GPIO_AUDIO high, GPIO_AUDIO_INV low */
#define SND_CODE_N              2       /* GPIO_AUDIO low, GPIO_AUDIO_INV high */

typedef struct {
        int32_t dc_q8;          /* Running DC estimate of the input, Q8 */
        uint8_t last;           /* Last input sample seen */
        uint8_t hang;           /* Consecutive constant frames while active */
        bool    active;
        bool    hi;             /* Slicer state */
} snd_state_t;

/* Convert one frame of Mac samples (unsigned 8-bit, 0x80 = silence) into
 * a 1-bit square wave on the two speaker pins.
 *
 * There's no amplifier, so the output is "all or nothing" for the loudest
 * result: the waveform (DC-removed, with a little hysteresis) is sliced
 * to above/below zero.  Volume is then 3-level:
 *   Mac volume 0                    off
 *   1 .. SND_VOL_FULL_MIN-1         half: pins swing 0V / +3.3V (one pin
 *                                   held low, so the speaker sees 3.3V p-p)
 *   SND_VOL_FULL_MIN .. 7           full: pins complementary, +3.3V / -3.3V
 *
 * lut[] maps a SND_CODE_* to the 32-bit value that gets written to the PWM
 * slice's compare register (each pin is simply held fully high or low).
 *
 * in may be NULL if the Mac has sound switched off.
 *
 * Returns false if the output is idle (and out[] isn't written): the caller
 * should park the speaker pins low.  Otherwise true and out[] holds
 * SND_FRAME_SAMPLES values.  A "hang" period lets a sound finish before
 * the output is parked.
 */
static inline bool snd_process(snd_state_t *st, const uint8_t *in, unsigned vol,
                               const uint32_t lut[3], uint32_t *out)
{
        bool constant = true;
        if (in) {
                for (unsigned i = 1; i < SND_FRAME_SAMPLES; i++) {
                        if (in[i] != in[0]) {
                                constant = false;
                                break;
                        }
                }
        }

        if (constant && (!st->active || st->hang >= SND_HANG_FRAMES)) {
                st->active = false;
                if (in)
                        st->last = in[0];
                return false;
        }
        if (constant)
                st->hang++;
        else
                st->hang = 0;

        if (!st->active) {
                st->active = true;
                st->hi = false;
                st->dc_q8 = (int32_t)(in ? in[0] : st->last) << 8;
        }

        const int32_t thr = SND_HYST << 8;
        const uint32_t low_code = (vol >= SND_VOL_FULL_MIN) ? SND_CODE_N : SND_CODE_LOW;
        for (unsigned i = 0; i < SND_FRAME_SAMPLES; i++) {
                uint8_t s = in ? in[i] : st->last;
                st->last = s;
                int32_t x = (int32_t)s << 8;
                st->dc_q8 += (x - st->dc_q8) / (1 << SND_DC_SHIFT);
                int32_t y = x - st->dc_q8;
                if (st->hi) {
                        if (y < -thr)
                                st->hi = false;
                } else {
                        if (y > thr)
                                st->hi = true;
                }
                uint32_t code = (vol == 0) ? SND_CODE_LOW : (st->hi ? SND_CODE_P : low_code);
                out[i] = lut[code];
        }
        return true;
}

void    snd_init(void);
void    snd_frame(void);        /* Call once per vsync */

#endif
