/* mem2scr: dump an XBM screencapture from a Mac 128/512 memory dump
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>


int main(int argc, char *argv[])
{
        int fd;
        struct stat sb;

	if (argc != 2) {
		printf("Syntax: %s <ram image>\n", argv[0]);
                return 1;
	}

        fd = open(argv[1], O_RDONLY);
        if (fd < 0) {
                printf("Can't open %s!\n", argv[1]);
                return 1;
        }

        fstat(fd, &sb);
        uint8_t *ram_base;
        ram_base = mmap(0, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (ram_base == MAP_FAILED) {
                printf("Can't mmap RAM!\n");
                return 1;
        }
        uint8_t *scr_base = ram_base;

        // ScrnBase = 0x01A700 (128K) or 0x07A700 (512K)
        // OK so.... check RAM is expected size:
        if (sb.st_size == 0x20000) {
                scr_base += 0x1a700;
        } else if (sb.st_size == 0x80000) {
                scr_base += 0x7a700;
        } else {
                printf("RAM size (%" PRId64 ") should be 128 or 512K! Trying to continue...\n", sb.st_size);
                scr_base += sb.st_size - 0x5900;
        }

        // Open out.xbm:
        const char *outfname = "out.xbm";
        FILE *outf = fopen(outfname, "w");
        if (!outf) {
                printf("Can't open %s!\n", outfname);
                return 1;
        }

        fprintf(outf, "#define out_width 512\n"
                "#define out_height 342\n"
                "static char out_bits[] = {\n");

        // Output L-R, big-endian shorts, with bits in MSB-LSB order:
        for (int y = 0; y < 342; y++) {
                for (int x = 0; x < 512; x += 16) {
                        uint8_t plo = scr_base[x/8 + (y * 512/8) + 0];
                        uint8_t phi = scr_base[x/8 + (y * 512/8) + 1];
                        uint8_t ob = 0;
                        for (int i = 0; i < 8; i++) {
                                ob |= (plo & (1 << i)) ? (0x80 >> i) : 0;
                        }
                        fprintf(outf, "%02x, ", ob);
                        ob = 0;
                        for (int i = 0; i < 8; i++) {
                                ob |= (phi & (1 << i)) ? (0x80 >> i) : 0;
                        }
                        fprintf(outf, "%02x, ", ob);
                }
        }
        fprintf(outf, "};\n");
        fclose(outf);
        return 0;
}
