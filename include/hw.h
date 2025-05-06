/*
 * pico-umac pin definitions
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

#ifndef HW_H
#define HW_H

#if PICO_ZERO /* Pin defs for Waveshare Pico Zero board */
#define GPIO_LED_PIN    16  /* Use Pico Zero's build-in ws2812 RGB neo-pixel */
#define GPIO_VID_DATA   26
#define GPIO_VID_VS     27
#define GPIO_VID_CLK    28
#define GPIO_VID_HS     29
#else /* Default RP Pico pins */
#define GPIO_LED_PIN    PICO_DEFAULT_LED_PIN 
#define GPIO_VID_DATA   18
#define GPIO_VID_VS     19
#define GPIO_VID_CLK    20
#define GPIO_VID_HS     21
#endif

#endif
