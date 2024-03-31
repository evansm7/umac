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

#ifndef CPU_CB_H
#define CPU_CB_H

#include <inttypes.h>
#include "machw.h"

/* Note unsigned int instead of uint32_t, to make types exactly match
 * Musashi ;(
 */
unsigned int    cpu_read_byte(unsigned int address);
unsigned int    cpu_read_word(unsigned int address);
unsigned int    cpu_read_long(unsigned int address);
void            cpu_write_byte(unsigned int address, unsigned int value);
void            cpu_write_word(unsigned int address, unsigned int value);
void            cpu_write_long(unsigned int address, unsigned int value);
void            cpu_pulse_reset(void);
void            cpu_set_fc(unsigned int fc);
int             cpu_irq_ack(int level);
void            cpu_instr_callback(int pc);

extern unsigned int (*cpu_read_instr)(unsigned int address);

/* This is special: an aligned 16b opcode, and will never act on MMIO.
 */
static inline unsigned int    cpu_read_instr_word(unsigned int address)
{
        return cpu_read_instr(address);
}

#endif
