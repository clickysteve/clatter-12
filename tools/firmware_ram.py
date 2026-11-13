#!/usr/bin/env python3
"""firmware_ram.py IMAGE... - how much static SRAM a Plinky 12 firmware image leaves a panel.

Copyright (C) 2026 The CLATTER contributors. SPDX-License-Identifier: GPL-3.0-only

Plinky does not publish the firmware's memory map, but it does publish whole firmware images: every
community panel is built by Plinky's own pipeline, against the newest firmware, into
github.com/plinkysynth/community-panels-built (`*.bin.lzma`, Git LFS; also reachable as
https://media.githubusercontent.com/media/plinkysynth/community-panels-built/main/<path>). Each image
is a pico-sdk RP2350 binary at 0x10000000, whose start-up code (crt0.S) carries:

  * the data copy table: (source in flash, destination in SRAM, end) triples, ending with 0, for .data
    (with the SRAM code, .time_critical) and the two scratch banks;
  * the .bss bounds it zeroes, in the literal pool after the table.

Main SRAM is 0x20000000-0x20080000 (512 KB; the 8 KB of scratch banks above it hold the stacks).
What lies between the end of .bss and 0x20080000 is all the static SRAM there is; the pico-sdk's
linker script also places the C library's heap there (PICO_HEAP_SIZE, 2 KB by default), and fails the
link ("region RAM overflowed") if .data + .bss + heap no longer fit. So a panel's own static data
(.data, .bss and SRAM code) must fit in the space this reports, less the heap.

The images also show (by difference) how much a given community panel adds. Nothing is executed:
the image is read as bytes. Usage: IMAGE may be a .bin or a .bin.lzma.
"""
import lzma, struct, sys

FLASH = 0x10000000
SRAM_END = 0x20080000


def load(path):
    data = open(path, "rb").read()
    return lzma.decompress(data) if path.endswith(".lzma") else data


def word(img, addr):
    return struct.unpack_from("<I", img, addr - FLASH)[0]


def analyse(img):
    sp, reset = word(img, FLASH), word(img, FLASH + 4) & ~1
    # pico-sdk crt0: after the core check, `adr r4, data_cpy_table` (0xa4xx) ... then the bss loop's
    # `ldr r1, =__bss_start__` (0x49xx) and `ldr r2, =__bss_end__` (0x4axx)
    pc, table, bss = reset, None, None
    for _ in range(64):
        hw = struct.unpack_from("<H", img, pc - FLASH)[0]
        if table is None and (hw & 0xff00) == 0xa400:
            table = ((pc + 4) & ~3) + (hw & 0xff) * 4
        elif table is not None and (hw & 0xff00) == 0x4900:
            nxt = struct.unpack_from("<H", img, pc + 2 - FLASH)[0]
            if (nxt & 0xff00) == 0x4a00:
                lit1 = ((pc + 4) & ~3) + (hw & 0xff) * 4
                lit2 = ((pc + 6) & ~3) + (nxt & 0xff) * 4
                bss = (word(img, lit1), word(img, lit2))
                break
        pc += 2
    if table is None or bss is None:
        raise ValueError("not a pico-sdk image this tool recognises (no crt0 data copy table)")
    copies, t = [], table
    while True:
        src, dst, end = word(img, t), word(img, t + 4), word(img, t + 8)
        if src == 0:
            break
        copies.append((src, dst, end))
        t += 12
    return {"sp": sp, "reset": reset, "copies": copies, "bss": bss, "size": len(img)}


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for path in sys.argv[1:]:
        r = analyse(load(path))
        data = [(d, e) for s, d, e in r["copies"] if d < SRAM_END]
        print("%s: %d bytes of flash image" % (path, r["size"]))
        for s, d, e in r["copies"]:
            print("  copied to SRAM %08x-%08x %7d B (.data and SRAM code)" % (d, e, e - d))
        b0, b1 = r["bss"]
        print("  .bss          %08x-%08x %7d B" % (b0, b1, b1 - b0))
        print("  free above .bss to the end of main SRAM: %d B (the heap, 2 KB by default, comes out of this)"
              % (SRAM_END - b1))
        if data:
            print("  static SRAM in use: %d B of 524288" % (b1 - 0x20000000))


if __name__ == "__main__":
    main()
