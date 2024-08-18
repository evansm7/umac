# Makefile for umac
#
# Builds Musashi as submodule, unix_main as SDL2 test application.
#
# Copyright 2024 Matt Evans
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

DEBUG ?= 0
MEMSIZE ?= 128

SOURCES = $(wildcard src/*.c)

MUSASHI = external/Musashi/
MUSASHI_SRC = $(MUSASHI)/m68kcpu.c $(MUSASHI)/m68kdasm.c $(MUSASHI)/m68kops.c $(MUSASHI)/softfloat/softfloat.c
MY_OBJS = $(patsubst %.c, %.o, $(SOURCES))
MUSASHI_OBJS = $(patsubst %.c, %.o, $(MUSASHI_SRC))
OBJS = $(MY_OBJS) $(MUSASHI_OBJS)

SDL_CFLAGS = $(shell sdl2-config --cflags)
SDL_LIBS = $(shell sdl2-config --libs)

LINKFLAGS =
LIBS = $(SDL_LIBS) -lm

INCLUDEFLAGS = -Iinclude/ -I$(MUSASHI) $(SDL_CFLAGS) -DMUSASHI_CNF=\"../include/m68kconf.h\"
INCLUDEFLAGS += -DENABLE_DASM=1
INCLUDEFLAGS += -DUMAC_MEMSIZE=$(MEMSIZE)
CFLAGS = $(INCLUDEFLAGS) -Wall -Wextra -pedantic -DSIM

ifeq ($(DEBUG),1)
	CFLAGS += -Og -g -ggdb -DDEBUG
endif

all:	main

$(MUSASHI_SRC): $(MUSASHI)/m68kops.h

$(MUSASHI)/m68kops.c $(MUSASHI)/m68kops.h:
	make -C $(MUSASHI) m68kops.c m68kops.h && ./tools/decorate_ops.py $(MUSASHI)/m68kops.c tools/fn_hot200.txt

prepare:	$(MUSASHI)/m68kops.c $(MUSASHI)/m68kops.h

%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

main:	$(OBJS)
	@echo Linking $(OBJS)
	$(CC) $(LINKFLAGS) $^ $(LIBS) -o $@

clean:
	make -C $(MUSASHI) clean
	rm -f $(MY_OBJS) main

################################################################################
# Mac driver sources (no need to generally rebuild
# Needs a m68k-linux-gnu binutils, but not GCC.

M68K_CROSS ?= m68k-linux-gnu-
M68K_AS = $(M68K_CROSS)as
M68K_LD = $(M68K_CROSS)ld
M68K_OBJCOPY = $(M68K_CROSS)objcopy

include/sonydrv.h:	sonydrv.bin
	xxd -i < $< > $@

.PHONY: sonydrv.bin
sonydrv.bin:	macsrc/sonydrv.S
	@# Yum hacky
	cpp $< | $(M68K_AS) -o sonydrv.o
	$(M68K_LD) sonydrv.o -o sonydrv.elf -Ttext=0
	$(M68K_OBJCOPY) sonydrv.elf -O binary --keep-section=.text $@
