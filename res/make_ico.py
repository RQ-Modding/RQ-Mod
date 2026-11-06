# Builds rq-mod.ico (48, 32 and 16 px, 32-bit DIB entries) from rq-mod.png.
# Standard library only. Run again whenever rq-mod.png changes:
#   python res/make_ico.py res/rq-mod.png res/rq-mod.ico
import struct, sys, zlib

def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, "need 8-bit RGB/RGBA, not interlaced"
            bpp = 3 if ctype == 2 else 4
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b, c = prev[i], prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append([tuple(line[x * bpp:x * bpp + 3]) + ((line[x * bpp + 3],) if bpp == 4 else (255,)) for x in range(w)])
        prev = line
    return w, h, rows

def resize(rows, w, h, size):
    # area average: each target pixel is the weighted mean of the source pixels it covers
    out = []
    for ty in range(size):
        row = []
        y0, y1 = ty * h / size, (ty + 1) * h / size
        for tx in range(size):
            x0, x1 = tx * w / size, (tx + 1) * w / size
            acc, total = [0.0] * 4, 0.0
            for sy in range(int(y0), min(h, int(y1 + 0.999))):
                wy = min(y1, sy + 1) - max(y0, sy)
                for sx in range(int(x0), min(w, int(x1 + 0.999))):
                    wgt = wy * (min(x1, sx + 1) - max(x0, sx))
                    if wgt <= 0: continue
                    for k in range(4): acc[k] += rows[sy][sx][k] * wgt
                    total += wgt
            row.append(tuple(int(round(v / total)) for v in acc))
        out.append(row)
    return out

def dib(rows, size):
    hdr = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    px = b"".join(bytes((p[2], p[1], p[0], p[3])) for row in reversed(rows) for p in row)
    mask = b"\0" * (((size + 31) // 32) * 4 * size)
    return hdr + px + mask

src, dst = sys.argv[1], sys.argv[2]
w, h, rows = read_png(src)
images = [(s, dib(rows if s == w else resize(rows, w, h, s), s)) for s in (48, 32, 16) if s <= w]
out = struct.pack("<HHH", 0, 1, len(images))
offset = 6 + 16 * len(images)
for s, d in images:
    out += struct.pack("<BBBBHHII", s, s, 0, 0, 1, 32, len(d), offset)
    offset += len(d)
open(dst, "wb").write(out + b"".join(d for _, d in images))
print("wrote", dst, [s for s, _ in images])
