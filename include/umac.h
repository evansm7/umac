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

#ifndef UMAC_H
#define UMAC_H

#include "disc.h"
#include "via.h"
#include "machw.h"

int     umac_init(void *_ram_base, void *_rom_base, disc_descr_t discs[DISC_NUM_DRIVES]);
int     umac_loop(void);
void    umac_reset(void);
void    umac_opt_disassemble(int enable);
void    umac_mouse(int deltax, int deltay, int button);
void    umac_kbd_event(uint8_t scancode, int down);

static inline void      umac_vsync_event(void)
{
        via_caX_event(2);
}

static inline void      umac_1hz_event(void)
{
        via_caX_event(1);
}

/* Return the offset into RAM of the current display buffer */
static inline unsigned int      umac_get_fb_offset(void)
{
        /* FIXME: Implement VIA RA6/vid.pg2 */
        return RAM_SIZE - 0x5900;
}

#endif
