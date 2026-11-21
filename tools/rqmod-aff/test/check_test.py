#!/usr/bin/env python3
"""Tests rqmod-aff check: writes one small Flash file per rule and reads what the check says.

    python check_test.py PATH/TO/rqmod-aff.exe [--keep DIR]

Each case is a file the checker must pass, warn about or refuse, with a piece of the text it
must say. --keep writes the files to DIR and leaves them there. Exit code 0 when every case
passes.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import zlib


# ---- writing SWF ----

class Bits:
    def __init__(self):
        self.v = 0
        self.n = 0

    def u(self, value, n):
        for i in range(n - 1, -1, -1):
            self.v = (self.v << 1) | ((value >> i) & 1)
            self.n += 1
        return self

    def s(self, value, n):
        return self.u(value & ((1 << n) - 1), n)

    def done(self):
        pad = (-self.n) % 8
        size = (self.n + pad) // 8
        return (self.v << pad).to_bytes(size, "big") if size else b""


def sbits(*values):
    n = 1
    for v in values:
        while not -(1 << (n - 1)) <= v < (1 << (n - 1)):
            n += 1
    return n


def rect(x0, x1, y0, y1):
    n = sbits(x0, x1, y0, y1)
    return Bits().u(n, 5).s(x0, n).s(x1, n).s(y0, n).s(y1, n).done()


def matrix(sx=None, sy=None, tx=0, ty=0):
    b = Bits()
    if sx is None:
        b.u(0, 1)
    else:
        n = sbits(sx, sy)
        b.u(1, 1).u(n, 5).s(sx, n).s(sy, n)
    b.u(0, 1)
    n = sbits(tx, ty) if tx or ty else 0
    b.u(n, 5)
    if n:
        b.s(tx, n).s(ty, n)
    return b.done()


def cxform(mult=None, add=None):
    values = tuple(mult or ()) + tuple(add or ())
    n = sbits(*values) if values else 1
    b = Bits().u(1 if add else 0, 1).u(1 if mult else 0, 1).u(n, 4)
    for v in tuple(mult or ()) + tuple(add or ()):
        b.s(v, n)
    return b.done()


def tag(code, body=b"", long=False):
    if len(body) < 0x3F and not long:
        return struct.pack("<H", code << 6 | len(body)) + body
    return struct.pack("<HI", code << 6 | 0x3F, len(body)) + body


def swf(tags, frames=1, rate=30 << 8, stage=(0, 2000, 0, 2000), version=10, length=None):
    body = rect(*stage) + struct.pack("<HH", rate, frames) + b"".join(tags)
    size = 8 + len(body)
    return b"FWS" + bytes([version]) + struct.pack("<I", size if length is None else length) + body


def cws(fws, raw=None):
    return b"CWS" + fws[3:4] + struct.pack("<I", len(fws) if raw is None else raw) + zlib.compress(fws[8:])


MASK = bytes([0xC8, 0xBB, 0x8F, 0xB7, 0xED, 0x43, 0x99, 0x4A, 0xA2, 0x7E, 0x5B, 0xB0, 0x68, 0x18, 0xF8, 0x88])


def aff(inner, version=1, xor=True):
    body = bytearray(inner)
    if xor:
        for i in range(min(0x40, len(body))):
            body[i] ^= MASK[i & 15]
    return b"AFF\0" + struct.pack("<III", version, 16 + len(body), 0x4D2) + bytes(body)


OPAQUE = bytes([255, 200, 60, 20])


def bitmap(cid, w=4, h=4, pixel=OPAQUE, code=36, fmt=5, data=None):
    if data is None:
        data = zlib.compress(pixel * (w * h))
    return tag(code, struct.pack("<HBHH", cid, fmt, w, h) + data, long=True)


def shape(sid, bmp, w=4, h=4, fill=0x41, lines=0, fill_bits=1, index=1, edges=None, style=0x05):
    x1, y1 = w * 20, h * 20
    body = struct.pack("<H", sid) + rect(0, x1, 0, y1)
    body += bytes([1, fill]) + struct.pack("<H", bmp) + matrix(20 << 16, 20 << 16)
    body += bytes([lines]) + b"\x14\x00\x00\x00\x00" * lines
    body += bytes([fill_bits << 4])
    b = Bits().u(style, 6)
    if style & 1:
        b.u(1, 5).s(0, 1).s(0, 1)
    b.u(index, fill_bits)
    for e in edges if edges is not None else [(x1, 0), (0, y1), (-x1, 0), (0, -y1)]:
        if e == "curve":
            b.u(1, 1).u(0, 1).u(0, 4).s(0, 2).s(0, 2).s(0, 2).s(0, 2)
            continue
        dx, dy = e
        v = dx if dx else dy
        nb = max(sbits(v), 2)
        b.u(1, 1).u(1, 1).u(nb - 2, 4).u(0, 1).u(1 if dy else 0, 1).s(v, nb)
    b.u(0, 6)
    return tag(2, body + b.done())


def place2(depth, char=None, move=False, mtx=None, cx=None, clip_depth=None):
    flags = (move and 1) | (char is not None and 2) | (mtx is not None and 4) | (cx is not None and 8) \
        | (clip_depth is not None and 0x40)
    body = bytes([flags]) + struct.pack("<H", depth)
    if char is not None:
        body += struct.pack("<H", char)
    if mtx is not None:
        body += mtx
    if cx is not None:
        body += cx
    if clip_depth is not None:
        body += struct.pack("<H", clip_depth)
    return tag(26, body)


def place3(depth, char=None, blend=None, filters=False):
    flags = (char is not None and 2) | (blend is not None and 0x0200) | (filters and 0x0100)
    body = struct.pack("<HH", flags, depth)
    if char is not None:
        body += struct.pack("<H", char)
    if filters:
        body += b"\x00"
    if blend is not None:
        body += bytes([blend])
    return tag(70, body)


def remove2(depth):
    return tag(28, struct.pack("<H", depth))


def sound(sid, samples=10, nbytes=None, fmt=3):
    flags = fmt << 4 | 3 << 2 | 1 << 1
    nbytes = samples * 2 if nbytes is None else nbytes
    return tag(14, struct.pack("<HBI", sid, flags, samples) + bytes(nbytes), long=True)


def start(sid, flags=0, extra=b""):
    return tag(15, struct.pack("<HB", sid, flags) + extra)


def sprite(sid, inner, frames=1):
    return tag(39, struct.pack("<HH", sid, frames) + b"".join(inner), long=True)


def action(*codes):
    return tag(12, b"".join(codes))


ATTRS = tag(69, b"\0\0\0\0")
SHOW = tag(1)
END = tag(0)
STOP = b"\x07"
PLAY = b"\x06"
A_END = b"\x00"


def goto(frame):
    return b"\x81" + struct.pack("<HH", 2, frame)


def body(*middle, place=True):
    """A file that plays: a picture, its shape and one object, with middle before the frame."""
    return [ATTRS, bitmap(1), shape(2, 1), *middle] + ([place2(1, char=2)] if place else []) + [SHOW, END]


def nested(depth):
    inner = [place2(1, char=2), SHOW, END]
    for i in range(depth):
        inner = [sprite(100 + i, inner), SHOW, END]
    return inner[0]


# ---- the cases: (name, file bytes, status, [(level, text), ...] or "clean") ----

def cases():
    good = swf(body())
    yield "a file that plays", good, 0, "clean"
    yield "compressed (CWS)", cws(good), 0, "clean"
    yield "as the game's .aff", aff(cws(good)), 0, "clean"
    yield "an .aff holding FWS", aff(good), 0, "clean"
    yield "LZMA", b"ZWS" + good[3:], 3, [("error", "LZMA (ZWS)")]
    yield "not Flash", b"\x89PNG\r\n\x1a\n" + bytes(16), 3, [("error", "neither AFF, FWS nor CWS")]
    yield "an .aff of another version", aff(good, version=2), 3, [("error", "header is not the game's")]
    yield "an .aff too short", b"AFF\0" + bytes(12), 3, [("error", "too short to hold a SWF")]
    yield "an .aff without the mask", aff(good, xor=False), 3, [("error", "does not hold a SWF")]
    yield "a wrong length", swf(body(), length=999), 3, [("error", "says it is 999 bytes long")]
    yield "a CWS that unpacks short", cws(good, raw=len(good) + 50), 3, [("error", "does not unpack to the")]
    yield "a CWS of 10 bytes", cws(good, raw=10), 3, [("error", "says it unpacks to 10 bytes")]
    yield "a cut header", b"FWS\x0a" + struct.pack("<I", 9) + b"\x78", 3, [("error", "ends inside its header")]
    yield "24 fps", swf(body(), rate=24 << 8), 0, [("warning", "24.00 fps")]
    yield "0 fps", swf(body(), rate=0), 0, [("warning", "frame rate is 0")]
    yield "0 frames", swf(body(), frames=0), 0, [("warning", "says 0 frames")]
    yield "fewer frames shown", swf(body(), frames=3), 0, [("warning", "holds frame 1 to the end")]
    yield "more frames shown", swf(body()[:-1] + [SHOW, END]), 0, [("warning", "stops after frame 1")]
    yield "a stage offset", swf(body(), stage=(200, 2200, 0, 2000)), 0, [("warning", "stage starts at (10, 0)")]

    yield "a palette bitmap", swf([ATTRS, bitmap(1, fmt=3), SHOW, END]), 1, [("error", "has format 3 (a palette)")]
    yield "a 0 x 0 bitmap", swf([ATTRS, bitmap(1, w=0, h=0), SHOW, END]), 1, [("error", "is 0 x 0 pixels")]
    yield "bitmap data too short", swf([ATTRS, bitmap(1, data=zlib.compress(bytes(10))), SHOW, END]), 1, \
        [("error", "does not unpack to its 4 x 4 pixels")]
    yield "colours not multiplied", swf([ATTRS, bitmap(1, pixel=bytes([128, 200, 60, 20])), SHOW, END]), 0, \
        [("warning", "not multiplied by its alpha")]
    yield "colours multiplied", swf([ATTRS, bitmap(1, pixel=bytes([128, 100, 30, 10])), SHOW, END]), 0, "clean"
    yield "a wide bitmap", swf([ATTRS, bitmap(1, w=8200, h=1), SHOW, END]), 0, [("warning", "over 8192 pixels")]

    yield "a colour fill", swf(body(shape(3, 1, fill=0x00))), 1, [("error", "filled with a colour")]
    yield "a gradient fill", swf(body(shape(3, 1, fill=0x10))), 1, [("error", "filled with a gradient")]
    yield "a repeating fill", swf(body(shape(3, 1, fill=0x40))), 1, [("error", "a repeating picture")]
    yield "lines", swf(body(shape(3, 1, lines=1))), 1, [("error", "has lines")]
    yield "a fill it lacks", swf(body(shape(3, 1, fill_bits=2, index=2))), 1, [("error", "uses fill 2 of its 1")]
    yield "three edges", swf(body(shape(3, 1, edges=[(80, 0), (0, 80), (-80, 0)]))), 1, \
        [("error", "not one rectangle")]
    yield "a curve", swf(body(shape(3, 1, edges=["curve", (0, 80), (-80, 0), (0, -80)]))), 1, \
        [("error", "not one rectangle")]
    yield "new styles", swf(body(shape(3, 1, style=0x15))), 1, [("error", "not one rectangle")]

    yield "a sound short of its samples", swf(body(sound(5, samples=10, nbytes=4), start(5))), 0, \
        [("warning", "says 10 samples but holds 4 bytes")]
    yield "a sound that plays", swf(body(sound(5), start(5))), 0, "clean"
    yield "a sound envelope", swf(body(sound(5), start(5, flags=0x08, extra=b"\x00"))), 1, [("error", "an envelope")]
    yield "a sound that repeats", swf(body(sound(5), start(5, flags=0x04, extra=b"\x02\x00"))), 1, \
        [("error", "a repeat count")]
    yield "a sound not defined", swf(body(start(5))), 0, [("warning", "which is not defined: the game skips it")]
    yield "an MP3 started", swf(body(sound(5, fmt=2), start(5))), 1, \
        [("error", "the game closes when frame 1 starts it")]
    yield "an MP3 never started", swf(body(sound(5, fmt=2))), 0, [("warning", "nothing starts it")]

    yield "a mask", swf(body(place2(2, char=2, clip_depth=3))), 1, [("error", "is a mask (a clip depth)")]
    yield "filters", swf(body(place3(2, char=2, filters=True))), 1, [("error", "has filters")]
    yield "a layer blend", swf(body(place3(2, char=2, blend=2))), 0, [("warning", "is drawn as normal")]
    yield "an add blend", swf(body(place3(2, char=2, blend=8))), 0, "clean"
    yield "a move where nothing is", swf(body(place2(5, move=True, mtx=matrix(tx=20, ty=20)))), 1, \
        [("error", "changes depth 5, where nothing is")]
    yield "alpha added", swf(body(place2(2, char=2, cx=cxform(add=(0, 0, 0, 50))))), 0, [("warning", "adds to alpha")]
    yield "colours multiplied and added", swf(body(place2(2, char=2, cx=cxform(mult=(128, 128, 128, 256),
                                                                             add=(10, 0, 0, 0))))), 0, "clean"
    yield "a removal", swf([ATTRS, bitmap(1), shape(2, 1), place2(1, char=2), SHOW, remove2(1), SHOW, END],
                           frames=2), 0, "clean"

    yield "stop, play and a jump", swf(body(action(STOP, PLAY, goto(0), A_END))), 0, "clean"
    yield "a string pushed", swf(body(action(b"\x96" + struct.pack("<H", 3) + b"\x00a\x00", A_END))), 1, \
        [("error", "push of a string")]
    yield "getURL", swf(body(action(b"\x83" + struct.pack("<H", 2) + b"\x00\x00", A_END))), 1, \
        [("error", "action 0x83")]
    yield "an action with no end", swf(body(action(STOP))), 1, [("error", "have no end")]
    yield "a jump past the end", swf(body(action(goto(5), A_END))), 0, [("warning", "past the last frame")]
    yield "an arithmetic action", swf(body(action(b"\x0a", A_END))), 1, \
        [("error", "action 0x0a")]

    yield "a sprite that plays", swf(body(sprite(10, [place2(1, char=2), SHOW, END]), place3(2, char=10, blend=8))), 0, \
        "clean"
    yield "a sprite of 2 frames", swf(body(sprite(10, [place2(1, char=2), SHOW, SHOW, END], frames=2),
                                           place2(2, char=10))), 1, [("error", "has 2 frames")]
    yield "a sprite with a script", swf(body(sprite(10, [action(STOP, A_END), SHOW, END]), place2(2, char=10))), 1, \
        [("error", "Sprite 10 holds")]
    yield "a sprite of two objects", swf(body(sprite(10, [place2(1, char=2), place2(2, char=2), SHOW, END]),
                                              place2(2, char=10))), 1, [("error", "more than one object")]
    yield "a sprite that moves", swf(body(sprite(10, [place2(1, move=True, mtx=matrix(tx=20)), SHOW, END]),
                                          place2(2, char=10))), 1, [("error", "moves an object it never placed")]
    yield "a sprite of a bitmap", swf(body(sprite(10, [place2(1, char=1), SHOW, END]), place2(2, char=10))), 1, \
        [("error", "not a shape")]
    yield "a sprite in too deep", swf(body(nested(9), place2(2, char=108))), 1, [("error", "too deep to check")]

    yield "a shape before its bitmap", swf([ATTRS, shape(2, 1), place2(1, char=2), SHOW, bitmap(1), END]), 1, \
        [("error", "is not defined before frame 1")]
    yield "a bitmap placed", swf(body(place2(2, char=1))), 1, [("error", "shows bitmap 1 itself")]
    yield "a character not defined", swf(body(place2(2, char=77))), 0, \
        [("warning", "which is not defined: the game draws nothing there")]
    yield "defined twice", swf(body(bitmap(1))), 0, [("warning", "defined twice")]
    yield "text", swf(body(tag(11, bytes(20)))), 1, [("error", "(tag code 11): the game closes when frame 1 plays")]
    yield "tags after the end", swf(body() + [SHOW]), 0, [("warning", "come after the End tag")]
    yield "a tag cut short", swf(body()[:-1] + [struct.pack("<H", 2 << 6 | 20) + bytes(5)]), 1, \
        [("error", "the file is cut short")]
    yield "a lone byte", swf(body()[:-1] + [b"\x00"]), 1, [("error", "ends inside a tag's header")]
    yield "a long header cut", swf(body()[:-1] + [struct.pack("<H", 2 << 6 | 0x3F) + b"\x01"]), 1, \
        [("error", "ends inside a tag's header")]
    yield "a placement cut", swf(body(tag(26, b"\x02"))), 1, [("error", "ends before its content does")]


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    exe = sys.argv[1]
    keep = sys.argv[3] if len(sys.argv) > 3 and sys.argv[2] == "--keep" else None
    folder = keep or tempfile.mkdtemp(prefix="rqmod-aff-test-")
    os.makedirs(folder, exist_ok=True)
    failed = 0
    count = 0
    for i, (name, data, status, want) in enumerate(cases(), 1):
        count += 1
        ext = ".aff" if data[:3] == b"AFF" else ".swf"
        path = os.path.join(folder, "case%02d%s" % (i, ext))
        with open(path, "wb") as f:
            f.write(data)
        run = subprocess.run([exe, "check", path, "--json"], capture_output=True)
        why = []
        try:
            report = json.loads(run.stdout.decode("utf-8"))
        except ValueError:
            report = None
            why.append("no JSON: " + run.stdout.decode("utf-8", "replace")[:200]
                       + run.stderr.decode("utf-8", "replace")[:200])
        if run.returncode != status:
            why.append("exit %d, not %d" % (run.returncode, status))
        if report is not None:
            problems = report.get("problems", [])
            if want == "clean":
                if problems:
                    why.append("problems: " + "; ".join(p["text"] for p in problems))
            else:
                for level, text in want:
                    if not any(p["level"] == level and text in p["text"] for p in problems):
                        why.append("no %s with \"%s\" in: %s" % (level, text,
                                                              "; ".join(p["level"] + ": " + p["text"] for p in problems)
                                                              or "nothing"))
        if why:
            failed += 1
            print("FAIL %2d %s: %s" % (i, name, " / ".join(why)))
        else:
            print("pass %2d %s" % (i, name))
    print("%d cases, %d failed%s" % (count, failed, "" if keep else ""))
    if not keep:
        for f in os.listdir(folder):
            os.remove(os.path.join(folder, f))
        os.rmdir(folder)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
