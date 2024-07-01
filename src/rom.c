/* umac ROM-patching code
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
#include <inttypes.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "machw.h"
#include "rom.h"

#ifdef DEBUG
#define RDBG(...)       printf(__VA_ARGS__)
#else
#define RDBG(...)       do {} while(0)
#endif

#define RERR(...)       fprintf(stderr, __VA_ARGS__)

#define ROM_PLUSv3_VERSION      0x4d1f8172
#define ROM_PLUSv3_SONYDRV      0x17d30

#define M68K_INST_NOP           0x4e71

////////////////////////////////////////////////////////////////////////////////
// Replacement drivers to thwack over the ROM

static const uint8_t sony_driver[] = {
#include "sonydrv.h"
};


////////////////////////////////////////////////////////////////////////////////

static uint32_t rom_get_version(uint8_t *rom_base)
{
#if BYTE_ORDER == LITTLE_ENDIAN
        return __builtin_bswap32(*(uint32_t *)rom_base);
#else
        return *(uint32_t *)rom_base);
#endif
}

/* Not perf-critical, so open-coding to support BE _and_ unaligned access */
#define ROM_WR32(offset, data) do {                                     \
                rom_base[(offset)+0] = ((data) >> 24) & 0xff;           \
                rom_base[(offset)+1] = ((data) >> 16) & 0xff;           \
                rom_base[(offset)+2] = ((data) >> 8) & 0xff;            \
                rom_base[(offset)+3] = (data) & 0xff;                   \
        } while (0)
#define ROM_WR16(offset, data) do {                             \
                rom_base[(offset)+0] = ((data) >> 8) & 0xff;    \
                rom_base[(offset)+1] = (data) & 0xff;           \
        } while (0)


static void     rom_patch_plusv3(uint8_t *rom_base)
{
        /* Inspired by patches in BasiliskII!
         */

        /* Disable checksum check by bodging out the comparison, an "eor.l d3, d1",
         * into a simple eor.l d1,d1:
         */
        ROM_WR16(0xd92, 0xb381 /* eor.l d1, d1 */);     // Checksum compares 'same' kthx

        /* Replace .Sony driver: */
        memcpy(rom_base + ROM_PLUSv3_SONYDRV, sony_driver, sizeof(sony_driver));
        /* Register the FaultyRegion for the Sony driver: */
        ROM_WR32(ROM_PLUSv3_SONYDRV + sizeof(sony_driver) - 4, PV_SONY_ADDR);

        /* To do:
         *
         * - No IWM init
         * - new Sound?
         */
#if UMAC_MEMSIZE > 128 && UMAC_MEMSIZE < 512
        /* Hack to change memtop: try out a 256K Mac :) */
        for (int i = 0x376; i < 0x37e; i += 2)
                ROM_WR16(i, M68K_INST_NOP);
        ROM_WR16(0x376, 0x2a7c); // moveal #RAM_SIZE, A5
        ROM_WR16(0x378, RAM_SIZE >> 16);
        ROM_WR16(0x37a, RAM_SIZE & 0xffff);
        /* That overrides the probed memory size, but
         * P_ChecksumRomAndTestMemory returns a failure code for
         * things that aren't 128/512.  Skip that:
         */
        ROM_WR16(0x132, 0x6000); // Bra (was BEQ)
        /* FIXME: We should also remove the memory probe routine, by
         * allowing the ROM checksum to fail (it returns failure, then
         * we carry on).  This avoids wild RAM addrs being accessed.
         */
#endif
}

int      rom_patch(uint8_t *rom_base)
{
        uint32_t v = rom_get_version(rom_base);
        int r = -1;
        /* See https://docs.google.com/spreadsheets/d/1wB2HnysPp63fezUzfgpk0JX_b7bXvmAg6-Dk7QDyKPY/edit#gid=840977089
         */
        switch(v) {
        case ROM_PLUSv3_VERSION:
                rom_patch_plusv3(rom_base);
                r = 0;
                break;

        default:
                RERR("Unknown ROM version %08x, no patching", v);
        }

        return r;
}

