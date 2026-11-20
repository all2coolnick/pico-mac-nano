/* Functions to output Macintosh Startup Beep using 600Hz square wave on GPIO_AUDIO / GPIO_AUDIO_INV (GPIO13/GPIO12 by default):
 *
 * Copyright 2025 Nick Gillard
 *
 * Code based on examples from Raspberry Pi Foundation.
 * The code initializes a PIO state machine on a given
 * GPIO and generates a square wave.

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
#include <inttypes.h>
#include <string.h>
#include "pico/time.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h" //The hardware PIO library
#include "hw.h"
#include "ws2812.h"
#include "audio.pio.h" //The pio header created after compilation

void beep() {
    
    put_pixel_blue(1); // Lets use blue to to indicate audio activity
    gpio_init(GPIO_AUDIO);
    gpio_set_dir(GPIO_AUDIO, GPIO_OUT);
    gpio_init(GPIO_AUDIO_INV);
    gpio_set_dir(GPIO_AUDIO_INV, GPIO_OUT);

    PIO pio = pio1;
    uint offset = pio_add_program(pio, &PIOBeep_program); //Attempt to load the program
    uint sm = pio_claim_unused_sm(pio, true); //Claim a free state machine on a PIO instance
    PIOBeep_program_init(pio, sm, offset, GPIO_AUDIO, GPIO_AUDIO_INV, 52000.0f); //Initialize the program - Clock divider max is 65536.0 (16-bit) = approx 1.9KHz. Settled on 26000.0 with 8 clock cycle pio program (52000 as we're running CPU at 200%)
    sleep_ms(700);
    pio_sm_set_enabled(pio, sm, false); // Stop state machine
    // Release all PIO resources
    pio_sm_clear_fifos(pio,sm);
    pio_sm_restart(pio,sm);
    pio_sm_unclaim(pio,sm);
    pio_remove_program(pio,&PIOBeep_program,offset);
    // Set both audio outs to zero so they are not left with speaker shorting between a high and low pin.
    gpio_put(GPIO_AUDIO, 0);
    gpio_put(GPIO_AUDIO_INV, 0);

    put_pixel_blue(0);
}
