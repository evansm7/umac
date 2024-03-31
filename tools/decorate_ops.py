#!/usr/bin/env python3
#
# Shittiest quick hack "Perlython" script to take a list of special
# (hot) function names and in-place edit the m68kops.c to decorate
# those function declarations with M68K_FAST_FUNC.
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

import os
import re
import sys

if len(sys.argv) != 3:
    print("Syntax: %s <C source> <fn list file>" % (sys.argv[0]))
    sys.exit(1)

cfile = sys.argv[1]
fnlistfile = sys.argv[2]

# Read function list first:
fns = []

with open(fnlistfile, 'r') as flf:
    for l in flf:
        fns.append(l.rstrip())

print("Read %d functions" % (len(fns)))

# Read entire C source, process, then write back out:

clines = []

with open(cfile, 'r') as cf:
    for l in cf:
        clines.append(l)

# Process the source, writing it out again:

# FIXME: From param/cmdline:
def decorate_func(fname):
    return "M68K_FAST_FUNC(%s)" % (fname)

def fn_special(fname):
    return fname in fns

with open(cfile, 'w') as cf:
    for l in clines:
        m = re.search(r'^static void ([^(]*)\(void\)$', l)
        if m and fn_special(m.group(1)):
            # Does function exist in special list?
            cf.write("static void %s(void) /* In SRAM */\n" % (decorate_func(m.group(1))))
        else:
            cf.write(l)
