/*
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

#ifndef VIA_H
#define VIA_H

#include <stdio.h>

/* Callbacks for various VIA events: */
struct via_cb {
        void (*ra_change)(uint8_t val);
        void (*rb_change)(uint8_t val);
        uint8_t (*ra_in)();
        uint8_t (*rb_in)();
        void (*sr_tx)(uint8_t val);
        void (*irq_set)(int status);
};

void    via_init(struct via_cb *cb);
void    via_write(unsigned int address, uint8_t data);
uint8_t via_read(unsigned int address);
void    via_tick(uint64_t time);
/* Trigger an event on CA1 or CA2: */
void    via_caX_event(int ca);
void    via_sr_rx(uint8_t val);

#endif
