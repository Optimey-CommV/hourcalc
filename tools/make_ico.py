# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later

r"""Build a multi-size Windows .ico from an SVG.

    python tools\make_ico.py <icon.svg> [--small <icon-small.svg>]
                             [--size N=<icon-N.svg> ...] <out.ico>

Frames: 16, 20, 24, 32, 40, 48, 64 and 256 px, all PNG-compressed (32-bit RGBA,
Windows Vista and later). The artwork is flat, so the scanlines are stored without a
predictor filter, which deflates smaller than any filter; the exe carries this file as
its icon resource and the source archive carries it again, so every byte counts.
--bmp-below N writes the frames smaller than N px as 32-bit BMP frames instead (the
older and much bigger form, 9.5 KB for the four small frames). When --small is given,
that SVG is rendered for the 16-32 px frames, the main SVG for the rest.

--size N=<svg> (repeatable, one per frame size) overrides the drawing for that
single frame, whatever --small says. This is how a dedicated drawing on a pixel
grid is used for one size, e.g. the app icon:

    python tools\make_ico.py assets\icon\app.svg --small assets\icon\app-16.svg
        --size 24=assets\icon\app-24.svg assets\icon\app.ico

Rendering uses resvg_py (no system fonts needed), the ICO container is written
by hand so the result is byte-exact and small.
"""

import argparse
import io
import os
import struct
import sys
import zlib

import resvg_py
from PIL import Image

SIZES = [16, 20, 24, 32, 40, 48, 64, 256]
PNG_FROM = 0           # frames of this size and larger are PNG-compressed (--bmp-below changes it)
SMALL_UP_TO = 32       # --small is used for sizes up to and including this


def render(svg_text, size):
    data = resvg_py.svg_to_bytes(svg_string=svg_text, width=size, height=size,
                                 skip_system_fonts=True)
    img = Image.open(io.BytesIO(bytes(data))).convert("RGBA")
    if img.size != (size, size):
        img = img.resize((size, size), Image.LANCZOS)
    return img


def _png_chunk(kind, data):
    body = kind + data
    return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)


def png_unfiltered(img):
    """A 32-bit RGBA PNG whose scanlines all use filter type 0. For flat artwork with a
    few colours the plain rows deflate better than any predictor."""
    w, h = img.size
    raw = img.tobytes()
    rows = b"".join(b"\x00" + raw[y * w * 4:(y + 1) * w * 4] for y in range(h))
    best = None
    for strategy in (zlib.Z_DEFAULT_STRATEGY, zlib.Z_FILTERED, zlib.Z_RLE):
        for mem_level in (8, 9):
            c = zlib.compressobj(9, zlib.DEFLATED, 15, mem_level, strategy)
            z = c.compress(rows) + c.flush()
            if best is None or len(z) < len(best):
                best = z
    return (b"\x89PNG\r\n\x1a\n" + _png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            _png_chunk(b"IDAT", best) + _png_chunk(b"IEND", b""))


def png_frame(img):
    """The smaller of Pillow's optimized PNG and the unfiltered one."""
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    plain = png_unfiltered(img)
    return plain if len(plain) < len(buf.getvalue()) else buf.getvalue()


def bmp_frame(img):
    w, h = img.size
    px = img.load()
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, w * h * 4, 0, 0, 0, 0)
    rows = bytearray()
    for y in range(h - 1, -1, -1):          # bottom-up
        for x in range(w):
            r, g, b, a = px[x, y]
            rows += bytes((b, g, r, a))
    mask_row = ((w + 31) // 32) * 4          # AND mask: all zero, alpha does the work
    return header + bytes(rows) + bytes(mask_row * h)


def build_ico(frames):
    """frames: list of (size, bytes) in ascending size order."""
    count = len(frames)
    out = bytearray(struct.pack("<HHH", 0, 1, count))
    offset = 6 + 16 * count
    for size, data in frames:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    for _, data in frames:
        out += data
    return bytes(out)


def size_override(text):
    """argparse type for --size: 'N=path.svg' with N one of the frame sizes."""
    number, sep, path = text.partition("=")
    number = number.strip()
    if not sep or not number.isdigit() or not path.strip():
        raise argparse.ArgumentTypeError("expected N=path.svg, got %r" % text)
    if int(number) not in SIZES:
        raise argparse.ArgumentTypeError("%s is not a frame size (%s)" % (
            number, ", ".join(str(s) for s in SIZES)))
    return int(number), path.strip()


def read_svg(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def main(argv):
    ap = argparse.ArgumentParser(description="Build a multi-size .ico from an SVG.")
    ap.add_argument("svg", help="main icon SVG (viewBox 0 0 256 256)")
    ap.add_argument("--small", help="optional simplified SVG used for 16-32 px")
    ap.add_argument("--size", action="append", default=[], type=size_override, metavar="N=SVG",
                    help="SVG for the single frame size N (repeatable; overrides --small and the "
                         "main SVG for that frame)")
    ap.add_argument("--bmp-below", type=int, default=PNG_FROM, metavar="N",
                    help="write the frames smaller than N px as 32-bit BMP (default: all frames are PNG)")
    ap.add_argument("out", help="output .ico path")
    args = ap.parse_args(argv)

    overrides = {}
    for size, path in args.size:
        if size in overrides:
            ap.error("--size %d given twice" % size)
        overrides[size] = path

    main_svg = read_svg(args.svg)
    small_svg = read_svg(args.small) if args.small else main_svg

    frames = []
    sources = []
    for size in SIZES:
        if size in overrides:
            svg, source = read_svg(overrides[size]), overrides[size]
        elif size <= SMALL_UP_TO:
            svg, source = small_svg, args.small or args.svg
        else:
            svg, source = main_svg, args.svg
        img = render(svg, size)
        frames.append((size, png_frame(img) if size >= args.bmp_below else bmp_frame(img)))
        sources.append((size, source))

    data = build_ico(frames)
    with open(args.out, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes, %d frames: %s)" % (
        args.out, len(data), len(frames), ", ".join(str(s) for s, _ in frames)))
    print("sources: " + ", ".join("%d=%s" % (s, os.path.basename(p)) for s, p in sources))


if __name__ == "__main__":
    main(sys.argv[1:])
