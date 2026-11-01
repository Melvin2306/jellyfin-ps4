#!/usr/bin/env python3
"""Renders sce_sys/icon0.png (512x512 RGB): the app's own mark, a rounded
"screen" outline with a play triangle inside (teal -> indigo gradient) on a dark
background. Deliberately not the Jellyfin logo. Pure Python (no PIL), 4x4 supersampling."""
import struct, zlib, sys, os

W = H = 512
SS = 4
BG = (14, 18, 26)
C1 = (0x2E, 0xD3, 0xB7)  # teal
C2 = (0x5B, 0x5F, 0xEF)  # indigo

def in_rrect(p, x0, y0, x1, y1, r):
    x, y = p
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def in_tri(p, t):
    (x, y) = p
    def s(a, b):
        return (b[0]-a[0])*(y-a[1]) - (b[1]-a[1])*(x-a[0])
    d1, d2, d3 = s(t[0], t[1]), s(t[1], t[2]), s(t[2], t[0])
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))

play = [(214, 176), (346, 256), (214, 336)]

def shape(p):
    ring = in_rrect(p, 76, 96, 436, 416, 64) and not in_rrect(p, 108, 128, 404, 384, 34)
    stand = in_rrect(p, 196, 430, 316, 448, 9)
    return ring or stand or in_tri(p, play)

rows = []
for y in range(H):
    row = bytearray([0])
    for x in range(W):
        cov = 0
        for sy in range(SS):
            for sx in range(SS):
                p = (x + (sx + 0.5) / SS, y + (sy + 0.5) / SS)
                if shape(p):
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
