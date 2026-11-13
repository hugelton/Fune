#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Wrap the FM-1 update loader's RAM image (loader.bin) into ota.bin, the file the package carries.

  fm1_loader_wrap.py LOADER.bin OTA.bin
  fm1_loader_wrap.py --check OTA.bin        unwrap and verify (heads, CRCs, every block)

ota.bin = an outer head (32 B) + an inner head (32 B, the address the SPL loads the image to) + blocks:
[clen u32][dlen u32][LZ4 block]; dlen is 4096 except in the last block, and a compressed block must fit in 4096 B.
A head is a CRC16 (CCITT, init 0) over a 30-byte body: <HIIBBH16s = data crc, a, b, attr, 0, 0, name.

Written for Kreisel from the file format (felucca-bsp's README: "the loader builds from this tree and gives the same
image"), not copied from Felucca's build script. The LZ4 block coder is a plain greedy one (spec:
https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md; min match 4, the last 5 bytes literals, the last match
starts at least 12 bytes before the end).
"""
import struct
import sys

LOADER_LOAD = 0x01C0A800            # where the SPL puts the image (loader/loader.ld: LDR ORIGIN)
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
LOADER_MAX = 0x14000                # loader/loader.ld: LDR LENGTH
BLOCK = 4096


def crc16(data, crc=0):
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else crc << 1
        crc &= 0xFFFF
    return crc


def head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def _lenbytes(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def lz4_compress(src):
    n, out, table = len(src), bytearray(), {}
    anchor = i = 0
    limit = n - 12
    while i < limit:
        key = bytes(src[i:i + 4])
        cand = table.get(key)
        table[key] = i
        if cand is None or i - cand > 65535:
            i += 1
            continue
        m = 4
        while i + m < n - 5 and src[cand + m] == src[i + m]:
            m += 1
        lit = i - anchor
        out.append((min(lit, 15) << 4) | min(m - 4, 15))
        if lit >= 15:
            out += _lenbytes(lit - 15)
        out += src[anchor:i]
        out += struct.pack("<H", i - cand)
        if m - 4 >= 15:
            out += _lenbytes(m - 4 - 15)
        i += m
        anchor = i
    lit = n - anchor
    out.append(min(lit, 15) << 4)
    if lit >= 15:
        out += _lenbytes(lit - 15)
    out += src[anchor:]
    return bytes(out)


def lz4_decompress(blk, size):
    out, i = bytearray(), 0
    while i < len(blk):
        tok = blk[i]
        i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = blk[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += blk[i:i + lit]
        i += lit
        if i >= len(blk):
            break
        off = blk[i] | blk[i + 1] << 8
        i += 2
        m = tok & 15
        if m == 15:
            while True:
                b = blk[i]
                i += 1
                m += b
                if b != 255:
                    break
        m += 4
        for _ in range(m):
            out.append(out[-off])
    if len(out) != size:
        raise ValueError(f"block decodes to {len(out)} B, expected {size}")
    return bytes(out)


def wrap(image):
    if not image or len(image) > LOADER_MAX:
        raise SystemExit(f"loader image is {len(image)} B (0 < size <= {LOADER_MAX})")
    blocks = bytearray()
    for i in range(0, len(image), BLOCK):
        raw = image[i:i + BLOCK]
        c = lz4_compress(raw)
        if len(c) > BLOCK or lz4_decompress(c, len(raw)) != raw:
            raise SystemExit(f"loader: block {i // BLOCK} does not compress below {BLOCK} B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def unwrap(ota):
    """-> the loader image; raises on any inconsistency"""
    def parse(h):
        if crc16(h[2:32]) != struct.unpack("<H", h[:2])[0]:
            raise ValueError("head CRC")
        return struct.unpack("<HIIBBH16s", h[2:32])
    dcrc, a, b, attr, _, _, name = parse(ota[:32])
    body = ota[32:32 + b]
    if a != 0x20 or len(body) != b or crc16(body) != dcrc:
        raise ValueError("outer head does not describe the body")
    icrc, size, load, _, _, _, _ = parse(body[:32])
    img, i = bytearray(), 32
    while i < len(body):
        clen, dlen = struct.unpack("<II", body[i:i + 8])
        img += lz4_decompress(body[i + 8:i + 8 + clen], dlen)
        i += 8 + clen
    if len(img) != size or crc16(img) != icrc:
        raise ValueError("inner head does not describe the image")
    return bytes(img), load


def main(argv):
    if len(argv) == 3 and argv[1] == "--check":
        img, load = unwrap(open(argv[2], "rb").read())
        print(f"ok: {len(img)} B image for {load:#x}")
        return 0
    if len(argv) != 3:
        print(__doc__)
        return 2
    ota = wrap(open(argv[1], "rb").read())
    open(argv[2], "wb").write(ota)
    print(f"ota.bin {len(ota)} B")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
