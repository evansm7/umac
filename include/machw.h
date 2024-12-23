/*
 * Copyright 2024 Matt Evans
 *
 * Portions of the {READ,WRITE}_{BYTE,WORD,LONG} macros are from
 * Musashi, Copyright 1998-2002 Karl Stenerud.
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

#ifndef MACHW_H
#define MACHW_H

#ifndef DEBUG
#define NDEBUG
#endif

#include <assert.h>
#include <inttypes.h>
#include "rom.h"

#define ROM_ADDR        0x400000        /* Regular base (and 0, when overlay=0 */
#define RAM_SIZE        (1024*UMAC_MEMSIZE)
/* The RAM can be (host-side) split into hi/low segments, which is
 * useful when running in an MCU (e.g. split between internal/external
 * memory and store framebuffer (in hi) internally).
 */
#ifndef RAM_SIZE_HI
#define RAM_SIZE_HI     0
#endif
#define RAM_SIZE_LO     (RAM_SIZE - RAM_SIZE_HI)
#define RAM_HIGH_ADDR   0x600000        /* Initial alias of all RAM in Mac mem map */

#define PV_SONY_ADDR    0xc00069        /* Magic address for replacement driver PV ops */

////////////////////////////////////////////////////////////////////////////////
// RAM accessors
//
// This isn't a wonderful place, but RAM access is needed in various files.

extern uint8_t *_ram_lo_base;
extern uint8_t *_rom_base;
#if RAM_SIZE_HI > 0
extern uint8_t *_ram_hi_base;
#endif

static inline uint8_t *ram_get_base(uint32_t addr)
{
#if RAM_SIZE_HI > 0
	if (addr >= RAM_SIZE_LO)
                return _ram_hi_base + (addr - RAM_SIZE_LO);
        else
#endif
                return _ram_lo_base + addr;
}

static inline uint8_t *rom_get_base(void)
{
        return _rom_base;
}

extern int overlay;

#define ADR24(x)                        ((x) & 0xffffff)

/* Let's do it like this:
 *
 * When overlay=1:  (reset)
 *  - ROM is at 0-0x10_0000 (hm, to 0x40_0000 pretty much) and 0x40_0000-0x50_0000
 *  - RAM is at 0x60_0000-0x80_0000
 *
 * When overlay=0:
 *  - ROM is at 0x40_0000-0x50_0000
 *  - RAM is at 0-0x40_0000
 *  - manuals say 0x60_0000-0x80_00000 is "unassigned", but may as well make RAM here too
 *
 * i.e. RAM is 60-80, or !overlay and 0.  And ROM is 40-50, or overlay and 0.
 */
#define IS_ROM(x)       (((ADR24(x) & 0xf00000) == ROM_ADDR) || (overlay && (ADR24(x) & 0xf00000) == 0))
/* RAM: always at 0x600000-0x7fffff, sometimes at 0 (0 most likely so check first!) */
#define IS_RAM(x)       ((!overlay && ((ADR24(x) & 0xc00000) == 0)) || ((ADR24(x) & 0xe00000) == RAM_HIGH_ADDR))

/* For regular power-of-two memory sizes, this should resolve to a
 * simple mask (i.e. be fast).  For non-Po2 (e.g. a Mac208K), this
 * involves a divide when an access is made off the end of memory.
 * But, that should never happen post-boot.
 */
#define CLAMP_RAM_ADDR(x) ((x) >= RAM_SIZE ? (x) % RAM_SIZE : (x))

#define IS_VIA(x)       ((ADR24(x) & 0xe80000) == 0xe80000)
#define IS_IWM(x)       ((ADR24(x) >= 0xdfe1ff) && (ADR24(x) < (0xdfe1ff + 0x2000)))
#define IS_SCC_RD(x)    ((ADR24(x) & 0xf00000) == 0x900000)
#define IS_SCC_WR(x)    ((ADR24(x) & 0xf00000) == 0xb00000)
#define IS_DUMMY(x)     (((ADR24(x) >= 0x800000) && (ADR24(x) < 0x9ffff8)) || ((ADR24(x) & 0xf00000) == 0x500000))
#define IS_TESTSW(x)    (ADR24(x) >= 0xf00000)

/* Our own BE read/write macros: unaligned 16b words are not required to
 * be supported, as M68K does not allow them!  But 32b dwords can be
 * only 16b aligned.
 */
__attribute__((unused)) static uint16_t _umac_read16(uint8_t *addr)
{
	uint16_t *r = (uint16_t *)addr;
	assert(((uintptr_t)addr & 1) == 0);
#if BYTE_ORDER == BIG_ENDIAN
	return *r;
#else
	return __builtin_bswap16(*r);
#endif
}

__attribute__((unused)) static uint32_t _umac_read32(uint8_t *addr)
{
	uint16_t *r = (uint16_t *)addr;
	assert(((uintptr_t)addr & 1) == 0);
	uint16_t l,h;
	h = r[0];
	l = r[1];
#if BYTE_ORDER == BIG_ENDIAN
	return ((uint32_t)h << 16) | l;
#else
	return ((uint32_t)__builtin_bswap16(h) << 16) | __builtin_bswap16(l);
#endif
}

__attribute__((unused)) static void _umac_write16(uint8_t *addr, uint16_t val)
{
	uint16_t *r = (uint16_t *)addr;
	assert(((uintptr_t)addr & 1) == 0);
#if BYE_ORDER == BIG_ENDIAN
	*r = val;
#else
	*r = __builtin_bswap16(val);
#endif
}

__attribute__((unused)) static void _umac_write32(uint8_t *addr, uint32_t val)
{
	uint16_t *r = (uint16_t *)addr;
	assert(((uintptr_t)addr & 1) == 0);
#if BYTE_ORDER == BIG_ENDIAN
	r[0] = (val >> 16);
	r[1] = (val & 0xffff);
#else
	r[0] = __builtin_bswap16(val >> 16);
	r[1] = __builtin_bswap16(val & 0xffff);
#endif
}

/* Specific RAM/ROM access: */

#define RAM_RD8(addr)                   (*(ram_get_base(addr)))
#define RAM_RD16(addr)                  _umac_read16(ram_get_base(addr))
#define RAM_RD_ALIGNED_BE16(addr)       _umac_read16(ram_get_base(addr))
#define RAM_RD32(addr)                  _umac_read32(ram_get_base(addr))

#define RAM_WR8(addr, val)              do { *(ram_get_base(addr)) = (val); } while(0)
#define RAM_WR16(addr, val)             _umac_write16(ram_get_base(addr), val)
#define RAM_WR32(addr, val)             _umac_write32(ram_get_base(addr), val)

#define ROM_RD8(addr)                   (_rom_base[(addr)])
#define ROM_RD16(addr)                  _umac_read16(&_rom_base[(addr)])
#define ROM_RD_ALIGNED_BE16(addr)       _umac_read16(&_rom_base[(addr)])
#define ROM_RD32(addr)                  _umac_read32(&_rom_base[(addr)])

#endif
