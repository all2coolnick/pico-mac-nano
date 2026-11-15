/* Mac sound output: PWM + DMA.  See snd.h.
 *
 * The two speaker pins are the A and B outputs of one PWM slice.  With no
 * amplifier the loudest output is a full-swing square wave, so each pin is
 * simply held fully high or fully low (compare level 0 or > wrap) per
 * sample, the two pins being opposites of each other.  A DMA channel,
 * paced by one of the DMA fractional timers at the Mac's sample rate,
 * copies 32-bit words (level B << 16 | level A) from a ring buffer into the
 * slice's compare register.  The emulator drops 370 new samples into the
 * ring each emulated vsync, so the CPU does very little.  The sample
 * values and 3-level volume come from snd_process() in snd.h.
 *
 * When there's nothing to play both pins are parked low (via SIO), so the
 * speaker isn't left with a DC bias or a carrier running through it.
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hw.h"
#include "snd.h"
#include "umac.h"

#if (((GPIO_AUDIO >> 1) & 7) != ((GPIO_AUDIO_INV >> 1) & 7)) || ((GPIO_AUDIO & 1) == (GPIO_AUDIO_INV & 1))
#error "GPIO_AUDIO and GPIO_AUDIO_INV must be the A and B outputs of one PWM slice (e.g. 10/11 or 12/13)"
#endif

#define SND_SLICE       ((GPIO_AUDIO >> 1) & 7)
#define RING_BITS       10
#define RING_LEN        (1u << RING_BITS)
#define RING_MASK       (RING_LEN - 1)
#define FILL_TARGET     400     /* Samples queued ahead when a frame is added */
#define FILL_MIN        100
#define FILL_MAX        600     /* + one frame must fit in the ring */

/* Mac frame period (vsync) is 16667us: 370 samples per that. */
/* Compare register values: a level above the wrap value means always high */
#define LVL_HI          256u
#define LVL_LO          0u
#define CC(a, b)        ((uint32_t)(a) | ((uint32_t)(b) << 16))
#define P_IS_B          (GPIO_AUDIO & 1)        /* Which channel GPIO_AUDIO is */
#define CC_PN(p, n)     (P_IS_B ? CC(n, p) : CC(p, n))
static const uint32_t lut[3] = {
        [SND_CODE_LOW] = CC_PN(LVL_LO, LVL_LO),
        [SND_CODE_P]   = CC_PN(LVL_HI, LVL_LO),
        [SND_CODE_N]   = CC_PN(LVL_LO, LVL_HI),
};

#define SND_RATE_X1000  ((370ull * 1000000ull * 1000ull) / 16667ull)    /* Hz * 1000 */

static uint32_t ring[RING_LEN] __attribute__((aligned(RING_LEN * 4)));
static unsigned int     wr;             /* Producer index into ring */
static int              dma_ch = -1;
static snd_state_t      st;
static bool             pins_pwm;

static inline unsigned int ring_rd(void)
{
        return (dma_hw->ch[dma_ch].read_addr - (uintptr_t)ring) / 4;
}

static void     dma_start(void)
{
        dma_channel_config c = dma_channel_get_default_config(dma_ch);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_ring(&c, false, RING_BITS + 2);
        channel_config_set_dreq(&c, dma_get_timer_dreq(0));
        dma_channel_configure(dma_ch, &c, &pwm_hw->slice[SND_SLICE].cc, ring, 0xffffffffu, false);
        dma_timer_claim(0);
        /* sys_clk * X / Y.  Use X = 2 for resolution: */
        uint32_t y = (uint32_t)(((uint64_t)clock_get_hz(clk_sys) * 2ull * 1000ull + SND_RATE_X1000 / 2) / SND_RATE_X1000);
        dma_timer_set_fraction(0, 2, y);
        dma_channel_start(dma_ch);
}

static void     pins_set_pwm(bool on)
{
        if (on == pins_pwm)
                return;
        pins_pwm = on;
        if (on) {
                gpio_set_function(GPIO_AUDIO, GPIO_FUNC_PWM);
                gpio_set_function(GPIO_AUDIO_INV, GPIO_FUNC_PWM);
        } else {
                gpio_put(GPIO_AUDIO, 0);
                gpio_put(GPIO_AUDIO_INV, 0);
                gpio_set_dir(GPIO_AUDIO, GPIO_OUT);
                gpio_set_dir(GPIO_AUDIO_INV, GPIO_OUT);
                gpio_set_function(GPIO_AUDIO, GPIO_FUNC_SIO);
                gpio_set_function(GPIO_AUDIO_INV, GPIO_FUNC_SIO);
        }
}

void    snd_init(void)
{
        pins_pwm = true;        /* Force first pins_set_pwm(false) to act */
        pins_set_pwm(false);

        pwm_config pc = pwm_get_default_config();
        pwm_config_set_clkdiv_int(&pc, 1);
        pwm_config_set_wrap(&pc, 255);
        pwm_init(SND_SLICE, &pc, true);

        for (unsigned int i = 0; i < RING_LEN; i++)
                ring[i] = lut[SND_CODE_LOW];
        pwm_hw->slice[SND_SLICE].cc = ring[0];

        dma_ch = dma_claim_unused_channel(true);
        dma_start();
        wr = FILL_TARGET;
        printf("Audio: PWM slice %d on GPIO%d/%d, DMA ch %d\n", SND_SLICE, GPIO_AUDIO, GPIO_AUDIO_INV, dma_ch);
}

void    snd_frame(void)
{
        uint8_t in[SND_FRAME_SAMPLES];
        uint32_t out[SND_FRAME_SAMPLES];
        uint8_t vol = 0;
        bool was_active = st.active;

        int on = umac_audio_get_frame(in, &vol);
        if (!snd_process(&st, on ? in : NULL, vol, lut, out)) {
                pins_set_pwm(false);
                return;
        }

        /* DMA count is finite (2^32 transfers, ~54 hours); restart if nearly out. */
        if (dma_hw->ch[dma_ch].transfer_count < 0x1000000u) {
                dma_channel_abort(dma_ch);
                dma_channel_set_read_addr(dma_ch, ring, false);
                dma_channel_set_trans_count(dma_ch, 0xffffffffu, true);
        }

        unsigned int rd = ring_rd();
        if (!was_active) {
                /* Fresh start: ring may hold stale audio; flush to centre. */
                for (unsigned int i = 0; i < RING_LEN; i++)
                        ring[i] = lut[SND_CODE_LOW];
                wr = (rd + FILL_TARGET) & RING_MASK;
                pins_set_pwm(true);
        } else {
                unsigned int fill = (wr - rd) & RING_MASK;
                if (fill < FILL_MIN || fill > FILL_MAX)
                        wr = (rd + FILL_TARGET) & RING_MASK;    /* Under/over-run: resync */
        }

        for (unsigned int i = 0; i < SND_FRAME_SAMPLES; i++) {
                ring[wr] = out[i];
                wr = (wr + 1) & RING_MASK;
        }
}
