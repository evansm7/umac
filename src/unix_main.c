/* umac SDL2 main application
 *
 * Opens an SDL2 window, allocates RAM/loads and patches ROM, routes
 * mouse/keyboard updates to umac, and blits framebuffer to the
 * display.
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
#include <fcntl.h>
#include <getopt.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "SDL.h"

#include "rom.h"
#include "umac.h"
#include "machw.h"
#include "disc.h"

#include "keymap_sdl.h"

static void     print_help(char *n)
{
        printf("Syntax: %s <options>\n"
               "\t-r <rom path>\t\tDefault 'rom.bin'\n"
               "\t-W <rom dump path>\tDump ROM after patching\n"
               "\t-d <disc path>\n"
               "\t-w\t\t\tEnable persistent disc writes (default R/O)\n"
               "\t-i\t\t\tDisassembled instruction trace\n", n);
}

#define DISP_SCALE      2

static uint32_t framebuffer[DISP_WIDTH*DISP_HEIGHT];

/* Blit a 1bpp FB to a 32BPP RGBA output.  SDL2 doesn't appear to support
 * bitmap/1bpp textures, so expand.
 */
static void     copy_fb(uint32_t *fb_out, uint8_t *fb_in)
{
        // Output L-R, big-endian shorts, with bits in MSB-LSB order:
        for (int y = 0; y < DISP_HEIGHT; y++) {
                for (int x = 0; x < DISP_WIDTH; x += 16) {
                        uint8_t plo = fb_in[x/8 + (y * 512/8) + 0];
                        uint8_t phi = fb_in[x/8 + (y * 512/8) + 1];
                        for (int i = 0; i < 8; i++) {
                                *fb_out++ = (plo & (0x80 >> i)) ? 0 : 0xffffffff;
                        }
                        for (int i = 0; i < 8; i++) {
                                *fb_out++ = (phi & (0x80 >> i)) ? 0 : 0xffffffff;
                        }
                }
        }
}

/**********************************************************************/

/* The emulator core expects to be given ROM and RAM pointers,
 * with ROM already pre-patched.  So, load the file & use the
 * helper to patch it, then pass it in.
 *
 * In an embedded scenario the ROM is probably const and in flash,
 * and so ought to be pre-patched.
 */
int     main(int argc, char *argv[])
{
        void *ram_base;
        void *rom_base;
        void *disc_base;
        char *rom_filename = "rom.bin";
        char *rom_dump_filename = NULL;
        char *ram_filename = "ram.bin";
        char *disc_filename = NULL;
        int ofd;
        int ch;
        int opt_disassemble = 0;
        int opt_write = 0;

        ////////////////////////////////////////////////////////////////////////
        // Args

        while ((ch = getopt(argc, argv, "r:d:W:ihw")) != -1) {
                switch (ch) {
                case 'r':
                        rom_filename = strdup(optarg);
                        break;

                case 'i':
                        opt_disassemble = 1;
                        break;

                case 'd':
                        disc_filename = strdup(optarg);
                        break;

                case 'w':
                        opt_write = 1;
                        break;

                case 'W':
                        rom_dump_filename = strdup(optarg);
                        break;

                case 'h':
                default:
                        print_help(argv[0]);
                        return 1;
                }
        }

        ////////////////////////////////////////////////////////////////////////
        // Load memories/discs

        printf("Opening ROM '%s'\n", rom_filename);
        ofd = open(rom_filename, O_RDONLY);
        if (ofd < 0) {
                perror("ROM");
                return 1;
        }

        struct stat sb;
        fstat(ofd, &sb);
        off_t _rom_size = sb.st_size;
        rom_base = mmap(0, _rom_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, ofd, 0);
        if (rom_base == MAP_FAILED) {
                printf("Can't mmap ROM!\n");
                return 1;
        }
        if (rom_patch(rom_base)) {
                printf("Failed to patch ROM\n");
                return 1;
        }
        if (rom_dump_filename) {
                int rfd = open(rom_dump_filename, O_CREAT | O_TRUNC | O_RDWR, 0655);
                if (rfd < 0) {
                        perror("ROM dump");
                        return 1;
                }
                ssize_t written = write(rfd, rom_base, _rom_size);
                if (written < 0) {
                        perror("ROM dump write");
                        return 1;
                }
                if (written < _rom_size) {
                        printf("*** WARNING: Short write to %s!\n",
                               rom_dump_filename);
                } else {
                        printf("Dumped ROM to %s\n", rom_dump_filename);
                }
                close(rfd);
        }

        /* Set up RAM, shared file map: */
        ofd = open(ram_filename, O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (ofd < 0) {
                perror("RAM");
                return 1;
        }
        if (ftruncate(ofd, RAM_SIZE)) {
                perror("RAM ftruncate");
                return 1;
        }
        ram_base = mmap(0, RAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, ofd, 0);
        if (ram_base == MAP_FAILED) {
                perror("RAM mmap");
                return 1;
        }
        printf("RAM mapped at %p\n", (void *)ram_base);

        disc_descr_t discs[DISC_NUM_DRIVES] = {0};

        if (disc_filename) {
                printf("Opening disc '%s'\n", disc_filename);
                // FIXME: >1 disc
                ofd = open(disc_filename, opt_write ? O_RDWR : O_RDONLY);
                if (ofd < 0) {
                        perror("Disc");
                        return 1;
                }

                fstat(ofd, &sb);
                size_t disc_size = sb.st_size;

                /* Discs are always _writable_ from the perspective of
                 * the Mac, but by default data is a MAP_PRIVATE copy
                 * and is not synchronised to the backing file.  If
                 * opt_write, we use MAP_SHARED and open the file RW,
                 * so writes persist to the disc image.
                 */
                disc_base = mmap(0, disc_size, PROT_READ | PROT_WRITE,
                                 opt_write ? MAP_SHARED : MAP_PRIVATE,
                                 ofd, 0);
                if (disc_base == MAP_FAILED) {
                        printf("Can't mmap disc!\n");
                        return 1;
                }
                printf("Disc mapped at %p, size %ld\n", (void *)disc_base, disc_size);

                discs[0].base = disc_base;
                discs[0].read_only = 0;         /* See above */
                discs[0].size = disc_size;
        }

        ////////////////////////////////////////////////////////////////////////
        // SDL/UI init

        SDL_Renderer *renderer;
        SDL_Texture *texture;

        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window *window = SDL_CreateWindow("umac",
                                              SDL_WINDOWPOS_UNDEFINED,
                                              SDL_WINDOWPOS_UNDEFINED,
                                              DISP_WIDTH * DISP_SCALE,
                                              DISP_HEIGHT * DISP_SCALE,
                                              0);
        if (!window) {
                perror("SDL window");
                return 1;
        }
        SDL_SetWindowGrab(window, SDL_TRUE);
        SDL_SetRelativeMouseMode(SDL_TRUE);

        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer) {
                perror("SDL renderer");
                return 1;
        }

        texture = SDL_CreateTexture(renderer,
                                    SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STREAMING,
                                    DISP_WIDTH,
                                    DISP_HEIGHT);
        if (!texture) {
                perror("SDL texture");
                return 1;
        }

        ////////////////////////////////////////////////////////////////////////
        // Emulator init

        umac_init(ram_base, rom_base, discs);
        umac_opt_disassemble(opt_disassemble);

        ////////////////////////////////////////////////////////////////////////
        // Main loop

        int done = 0;
        uint64_t loops = 0;
        int mouse_button = 0;
        uint64_t last_vsync = 0;
        uint64_t last_1hz = 0;
        do {
                struct timeval tv_now;
                SDL_Event event;
                int mousex = 0;
                int mousey = 0;
                loops++;

                if (SDL_PollEvent(&event)) {
                        switch (event.type) {
                        case SDL_QUIT:
                                done = 1;
                                break;

                        case SDL_KEYDOWN:
                        case SDL_KEYUP: {
                                int c = SDLScan2MacKeyCode(event.key.keysym.scancode);
                                c = (c << 1) | 1;
                                printf("Key 0x%x -> 0x%x\n", event.key.keysym.scancode, c);
                                if (c != MKC_None)
                                        umac_kbd_event(c, (event.type == SDL_KEYDOWN));
                        } break;

                        case SDL_MOUSEMOTION:
                                mousex = event.motion.xrel;
                                mousey = -event.motion.yrel;
                                break;

                        case SDL_MOUSEBUTTONDOWN:
                                mouse_button = 1;
                                break;

                        case SDL_MOUSEBUTTONUP:
                                mouse_button = 0;
                                break;
                        }
                }

                umac_mouse(mousex, mousey, mouse_button);

                done |= umac_loop();

                gettimeofday(&tv_now, NULL);
                uint64_t now_usec = (tv_now.tv_sec * 1000000) + tv_now.tv_usec;

                /* Passage of time: */
                if ((now_usec - last_vsync) >= 16667) {
                        umac_vsync_event();
                        last_vsync = now_usec;

                        /* Cheapo framerate limiting: */
                        copy_fb(framebuffer, ram_get_base() + umac_get_fb_offset());
                        SDL_UpdateTexture(texture, NULL, framebuffer,
                                          DISP_WIDTH * sizeof (Uint32));
                        /* Scales texture up to window size */
                        SDL_RenderCopy(renderer, texture, NULL, NULL);
                        SDL_RenderPresent(renderer);
                }
                if ((now_usec - last_1hz) >= 1000000) {
                        umac_1hz_event();
                        last_1hz = now_usec;
                }
        } while (!done);

        return 0;
}
