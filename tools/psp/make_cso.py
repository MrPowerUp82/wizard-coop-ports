#!/usr/bin/env python3
"""Compresses a PSP ISO into CSO (CISO v1), the format PSP CFW, Adrenaline and PPSSPP read.

Layout: 24-byte header ("CISO", header size, total bytes, block size 2048, version 1, index
alignment 0), then (blocks + 1) little-endian uint32 offsets, then each block raw-deflated.
The top bit of an index entry marks a block stored uncompressed (used when deflate doesn't help).

    python3 make_cso.py game.iso game.cso [level]
"""
import struct
import sys
import zlib

BLOCK = 2048


def compress(src, dst, level=9):
    with open(src, 'rb') as f:
        data = f.read()
    blocks = (len(data) + BLOCK - 1) // BLOCK
    header = struct.pack('<4sIQIBB2x', b'CISO', 24, len(data), BLOCK, 1, 0)
    index_size = (blocks + 1) * 4
    offset = len(header) + index_size
    index, payload = [], []
    for i in range(blocks):
        block = data[i * BLOCK:(i + 1) * BLOCK].ljust(BLOCK, b'\0')
        c = zlib.compressobj(level, zlib.DEFLATED, -15)
        packed = c.compress(block) + c.flush()
        if len(packed) >= BLOCK:
            index.append(offset | 0x80000000)
            payload.append(block)
            offset += BLOCK
        else:
            index.append(offset)
            payload.append(packed)
            offset += len(packed)
    index.append(offset)
    if offset >= 1 << 31:
        raise SystemExit('ISO too large for CISO v1 without index alignment')
    with open(dst, 'wb') as f:
        f.write(header)
        f.write(struct.pack('<%dI' % len(index), *index))
        for chunk in payload:
            f.write(chunk)
    return len(data), offset


def verify(iso, cso):
    """Decompresses the CSO back and compares it with the ISO byte for byte."""
    with open(iso, 'rb') as f:
        original = f.read()
    with open(cso, 'rb') as f:
        blob = f.read()
    magic, _, total, block, _, _ = struct.unpack('<4sIQIBB2x', blob[:24])
    assert magic == b'CISO' and block == BLOCK and total == len(original)
    count = (total + block - 1) // block
    index = struct.unpack('<%dI' % (count + 1), blob[24:24 + (count + 1) * 4])
    out = bytearray()
    for i in range(count):
        start, end = index[i] & 0x7fffffff, index[i + 1] & 0x7fffffff
        chunk = blob[start:end]
        out += chunk if index[i] & 0x80000000 else zlib.decompress(chunk, -15)
    assert bytes(out[:total]) == original, 'CSO does not decompress to the ISO'


if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]
    level = int(sys.argv[3]) if len(sys.argv) > 3 else 9
    raw, packed = compress(src, dst, level)
    verify(src, dst)
    print(f'{dst}: {raw} -> {packed} bytes ({100 * packed / raw:.0f}%), verified')
