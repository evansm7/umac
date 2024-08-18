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

#ifndef DISC_H
#define DISC_H

#include <inttypes.h>

typedef int (*disc_op_read)(void *ctx, uint8_t *data, unsigned int offset, unsigned int len);
typedef int (*disc_op_write)(void *ctx, uint8_t *data, unsigned int offset, unsigned int len);
typedef struct {
        uint8_t *base;
        unsigned int size;
        int read_only;
        void *op_ctx;
        disc_op_read op_read;
        disc_op_write op_write;
} disc_descr_t;

#define DISC_NUM_DRIVES         2

/* Passed an array of descriptors of disc data:
 * FIXME: provide callbacks to fops->read/write etc. instead of needing
 * a flat array.
 *
 * Contents copied, pointer is not stored.
 */
void    disc_init(disc_descr_t discs[DISC_NUM_DRIVES]);
int     disc_pv_hook(uint8_t opcode);

#endif
