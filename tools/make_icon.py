#!/usr/bin/env python3
"""Renders sce_sys/icon0.png (512x512 RGB): a Jellyfin-style mark (a triangle
outline around a smaller filled triangle, purple -> blue gradient) on a dark
background. Pure Python (no PIL), 4x4 supersampling."""
import struct, zlib, sys, os

W = H = 512
SS = 4
BG = (16, 16, 20)
C1 = (0xAA, 0x5C, 0xC3)  # Jellyfin purple
C2 = (0x00, 0xA4, 0xDC)  # Jellyfin blue

def tri(cx, top, bottom, half):
    return [(cx, top), (cx + half, bottom), (cx - half, bottom)]

def inside(p, t):
    (x, y) = p
    def s(a, b):
        return (b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0])
    d1, d2, d3 = s(t[0], t[1]), s(t[1], t[2]), s(t[2], t[0])
    neg = d1 < 0 or d2 < 0 or d3 < 0
    pos = d1 > 0 or d2 > 0 or d3 > 0
    return not (neg and pos)

outer = tri(256, 70, 420, 205)
hole = tri(256, 150, 382, 135)
inner = tri(256, 228, 352, 62)

rows = []
for y in range(H):
    row = bytearray([0])
    for x in range(W):
        cov = 0
        for sy in range(SS):
            for sx in range(SS):
                p = (x + (sx + 0.5) / SS, y + (sy + 0.5) / SS)
                if (inside(p, outer) and not inside(p, hole)) or inside(p, inner):
                    cov += 1
        a = cov / (SS * SS)
        t = min(1.0, max(0.0, (x + y - 140) / 620))
        col = [C1[i] + (C2[i] - C1[i]) * t for i in range(3)]
        row += bytes(int(BG[i] * (1 - a) + col[i] * a + 0.5) for i in range(3))
    rows.append(bytes(row))

def chunk(tag, data):
    c = struct.pack('>I', len(data)) + tag + data
    return c + struct.pack('>I', zlib.crc32(tag + data) & 0xffffffff)

png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0)) \
    + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'sce_sys', 'icon0.png')
open(out, 'wb').write(png)
print('wrote', out, len(png), 'bytes')
