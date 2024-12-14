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
#include <getopt.h>
#include <arpa/inet.h> // for ntohl etc.

#define MACVAR_scrnBase         0x824   // u32
#define MACVAR_scrnXres         0x83a   // u16
#define MACVAR_scrnYres         0x838   // u16

static void help(char *me)
{
	printf("Syntax: %s [-i] <ram image>\n"
	       "\t-i\tInfer screen base from RAM size (512x342 only)\n"
	       , me);
}

int main(int argc, char *argv[])
{
        int fd;
        struct stat sb;
        int ch;
	int infer = 0;
	unsigned int xres = 512;
	unsigned int yres = 342;

        while ((ch = getopt(argc, argv, "hi")) != -1) {
		switch (ch) {
		case 'i':
			infer = 1;
			break;
		case 'h':
		default:
			help(argv[0]);
			return 1;
		}
        }

	if (optind >= argc) {
		help(argv[0]);
		return 1;
	}

        fd = open(argv[optind], O_RDONLY);
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

	if (infer) {
		// Old-style, for fixed 512x342 res
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
	} else {
		uint32_t mb = ntohl(*(uint32_t *)(ram_base + MACVAR_scrnBase));
		xres = ntohs(*(uint16_t *)(ram_base + MACVAR_scrnXres));
		yres = ntohs(*(uint16_t *)(ram_base + MACVAR_scrnYres));
		printf("Read screenbase at %x, %dx%d\n", mb, xres, yres);
		scr_base += mb;
	}

        // Open out.xbm:
        const char *outfname = "out.xbm";
        FILE *outf = fopen(outfname, "w");
        if (!outf) {
                printf("Can't open %s!\n", outfname);
                return 1;
        }

        fprintf(outf, "#define out_width %d\n"
                "#define out_height %d\n"
                "static char out_bits[] = {\n", xres, yres);

        // Output L-R, big-endian shorts, with bits in MSB-LSB order:
        for (int y = 0; y < yres; y++) {
                for (int x = 0; x < xres; x += 16) {
                        uint8_t plo = scr_base[x/8 + (y * xres/8) + 0];
                        uint8_t phi = scr_base[x/8 + (y * xres/8) + 1];
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
