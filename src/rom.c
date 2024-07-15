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
#define ROM_WR8(offset, data) do {                              \
                rom_base[(offset)+0] = (data) & 0xff;           \
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

#if DISP_WIDTH != 512 || DISP_HEIGHT != 342
#define SCREEN_SIZE                     (DISP_WIDTH*DISP_HEIGHT/8)
#define SCREEN_DISTANCE_FROM_TOP        (SCREEN_SIZE + 0x380)
#if (SCREEN_DISTANCE_FROM_TOP >= 65536)
#error "rom.c: Screen res patching maths won't work for a screen this large"
#endif
#define SCREEN_BASE                     (0x400000-SCREEN_DISTANCE_FROM_TOP)
#define SCREEN_BASE_L16                 (SCREEN_BASE & 0xffff)
#define SBCOORD(x, y)                   (SCREEN_BASE + ((DISP_WIDTH/8)*(y)) + ((x)/8))

        /* Changing video res:
         *
         * Original 512*342 framebuffer is 0x5580 bytes; the screen
         * buffer lands underneath sound/other buffers at top of mem,
         * i,e, 0x3fa700 = 0x400000-0x5580-0x380.  So any new buffer
         * will be placed (and read out from for the GUI) at
         * MEM_TOP-0x380-SCREEN_SIZE.
         *
         * For VGA, size is 0x9600 bytes (0x2580 words)
         */

        /* We need some space, low down, to create jump-out-and-patch
         * routines where a patch is too large to put inline.  The
         * TestSoftware check at 0x42 isn't used:
         */
        ROM_WR16(0x42, 0x6000);                 /* bra */
        ROM_WR16(0x44, 0x62-0x44);              /* offset */
        /* Now 0x46-0x57 can be used */
        unsigned int patch_0 = 0x46;
        ROM_WR16(patch_0 + 0, 0x9bfc);          /* suba.l #imm32, A5 */
        ROM_WR16(patch_0 + 2, 0);               /* (Could add more here) */
        ROM_WR16(patch_0 + 4, SCREEN_DISTANCE_FROM_TOP);
        ROM_WR16(patch_0 + 6, 0x6000);          /* bra */
        ROM_WR16(patch_0 + 8, 0x3a4 - (patch_0 + 8));   /* Return to 3a4 */

        /* Magic screen-related locations in Mac Plus ROM 4d1f8172:
         *
         * 8c : screen base addr (usually 3fa700, now 3f6680)
         * 148 : screen base addr again
         * 164 : u32 screen address of crash Mac/critErr hex numbers
         * 188 : u16 bytes per row (critErr)
         * 194 : u16 bytes per row (critErr)
         * 19c : u16 (bytes per row * 6)-1 (critErr)
         * 1a4 : u32 screen address of critErr twiddly pattern
         * 1ee : u16 screen sie in words minus one
         * 3a2 : u16 screen size in bytes (BUT can't patch immediate)
         * 474 : u16 bytes per row
         * 494 : u16 screen y
         * 498 : u16 screen x
         * a0e : y
         * a10 : x
         * ee2 : u16 bytes per row minus 4 (tPutIcon)
         * ef2 : u16 bytes per row (tPutIcon)
         * 7e0 : u32 screen address of disk icon (240, 145)
         * 7f2 : u32 screen address of disk icon's symbol (248, 160)
         * f0c : u32 screen address of Mac icon (240, 145)
         * f18 : u32 screen address of Mac icon's face (248, 151)
         * f36 : u16 bytes per row minus 2 (mPutSymbol)
         * 1cd1 : hidecursor's bytes per line
         * 1d48 : xres minus 32 (for cursor rect clipping)
         * 1d4e : xres minus 32
         * 1d74 : y
         * 1d93 : bytes per line (showcursor)
         * 1e68 : y
         * 1e6e : x
         * 1e82 : y
         */
        ROM_WR16(0x8c, SCREEN_BASE_L16);
        ROM_WR16(0x148, SCREEN_BASE_L16);
        ROM_WR32(0x164, SBCOORD(DISP_WIDTH/2 - (48/2), DISP_HEIGHT/2 + 8));
        ROM_WR16(0x188, DISP_WIDTH/8);
        ROM_WR16(0x194, DISP_WIDTH/8);
        ROM_WR16(0x19c, (6*DISP_WIDTH/8)-1);
        ROM_WR32(0x1a4, SBCOORD(DISP_WIDTH/2 - 8, DISP_HEIGHT/2 + 8 + 8));
        ROM_WR16(0x1ee, (SCREEN_SIZE/4)-1);

        ROM_WR32(0xf0c, SBCOORD(DISP_WIDTH/2 - 16, DISP_HEIGHT/2 - 26));
        ROM_WR32(0xf18, SBCOORD(DISP_WIDTH/2 - 8, DISP_HEIGHT/2 - 20));
        ROM_WR32(0x7e0, SBCOORD(DISP_WIDTH/2 - 16, DISP_HEIGHT/2 - 26));
        ROM_WR32(0x7f2, SBCOORD(DISP_WIDTH/2 - 8, DISP_HEIGHT/2 - 11));

        /* Patch "SubA #$5900, A5" to subtract 0x9880.
         * However... can't just patch the int16 immediate, as that's
         * sign-extended (and we end up with a subtract-negative,
         * i.e. an add).  There isn't space here to turn it into sub.l
         * so add some rigamarole to branch to some bytes stolen at
         * patch_0 up above.
         */
        ROM_WR16(0x3a0, 0x6000);                /* bra */
        ROM_WR16(0x3a2, patch_0 - 0x3a2);       /* ...to patch0, returns at 0x3a4 */

        ROM_WR16(0x474, DISP_WIDTH/8);
        ROM_WR16(0x494, DISP_HEIGHT);
        ROM_WR16(0x498, DISP_WIDTH);
        ROM_WR16(0xa0e, DISP_HEIGHT);           /* copybits? */
        ROM_WR16(0xa10, DISP_WIDTH);
        ROM_WR16(0xee2, (DISP_WIDTH/8)-4);      /* tPutIcon bytes per row, minus 4 */
        ROM_WR16(0xef2, DISP_WIDTH/8);          /* tPutIcon bytes per row */
        ROM_WR16(0xf36, (DISP_WIDTH/8)-2);      /* tPutIcon bytes per row, minus 2 */
        ROM_WR8(0x1cd1, DISP_WIDTH/8);          /* hidecursor */
        ROM_WR16(0x1d48, DISP_WIDTH-32);        /* 1d46+2 was originally 512-32 rite? */
        ROM_WR16(0x1d4e, DISP_WIDTH-32);        /* 1d4c+2 is 480, same */
        ROM_WR16(0x1d6e, DISP_HEIGHT-16);       /* showcursor (YESS fixed Y crash bug!) */
        ROM_WR16(0x1d74, DISP_HEIGHT);          /* showcursor */
        ROM_WR8(0x1d93, DISP_WIDTH/8);          /* showcursor */
        ROM_WR16(0x1e68, DISP_HEIGHT);          /* mScrnSize */
        ROM_WR16(0x1e6e, DISP_WIDTH);           /* mScrnSize */
        ROM_WR16(0x1e82, DISP_HEIGHT);          /* tScrnBitMap */

        /* FIXME: Welcome To Macintosh is drawn at the wrong position. Find where that's done. */
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

