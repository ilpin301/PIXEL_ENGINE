#!/usr/bin/env python3
"""Asset converter for the pixel engine: PNG -> sprite/tileset/font blob, palette, Tiled/CSV -> tilemap blob.

Blob = 12-byte header ("PGEA", type u8, version u8 = 1, u16 0, u32 payload size) + payload, little-endian.
"""
import argparse
import csv
import json
import os
import re
import struct
import sys
import tempfile

from PIL import Image

T_SPRITES, T_TILESET, T_FONT, T_TILEMAP, T_STRINGS = 0, 1, 2, 6, 7
FLIP_MASK = 0xF0000000  # Tiled flip/rotate flag bits (plus a spare nibble bit)


class ToolError(Exception):
    pass


def blob(typ, payload):
    return b"PGEA" + struct.pack("<BBHI", typ, 1, 0, len(payload)) + payload


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ---------------------------------------------------------------- images

def open_rgba(path):
    try:
        return Image.open(path).convert("RGBA")
    except OSError as e:
        raise ToolError(f"cannot read image {path}: {e}")


def load_pixels(path):
    """Row-major RGB565 values, None where alpha < 128. Returns (w, h, pixels)."""
    img = open_rgba(path)
    raw = img.tobytes()
    px = [None if raw[i + 3] < 128 else rgb565(raw[i], raw[i + 1], raw[i + 2])
          for i in range(0, len(raw), 4)]
    return img.width, img.height, px


def index_pixels(stream):
    """Palette by first appearance of the RGB565 value in stream. Returns (palette, indices, has_transparent)."""
    transparent = any(p is None for p in stream)
    palette = [0x0000] if transparent else []  # index 0 reserved for transparent
    lookup = {}
    out = bytearray()
    for p in stream:
        if p is None:
            out.append(0)
            continue
        if p not in lookup:
            if len(palette) == 256:
                raise ToolError("too many colours: more than 256 palette entries")
            lookup[p] = len(palette)
            palette.append(p)
        out.append(lookup[p])
    return palette, bytes(out), transparent


def sprites_blob(path, fw, fh, atlas=False, typ="sprites"):
    if fw < 1 or fh < 1:
        raise ToolError("frame size must be positive")
    if typ == "tileset" and (atlas or fw > 255 or fh > 255):
        raise ToolError("tileset needs stacked frames (no --atlas) and w,h <= 255")
    W, H, px = load_pixels(path)
    if W % fw or H % fh:
        raise ToolError(f"image {W}x{H} is not a multiple of frame {fw}x{fh}")
    cols, rows = W // fw, H // fh
    if atlas:
        stream = px
    else:
        stream = [px[(ry * fh + y) * W + cx * fw + x]
                  for ry in range(rows) for cx in range(cols)
                  for y in range(fh) for x in range(fw)]
    palette, idx, transparent = index_pixels(stream)
    frames = cols * rows
    if frames > 0xFFFF or (atlas and cols > 0xFFFF):
        raise ToolError("too many frames")
    sub = struct.pack("<HHHHHBB", fw, fh, frames, cols if atlas else 0, len(palette), int(transparent), 0)
    payload = sub + struct.pack(f"<{len(palette)}H", *palette) + idx
    return blob(T_TILESET if typ == "tileset" else T_SPRITES, payload)


def palette_list(path):
    return index_pixels(load_pixels(path)[2])[0]


# ---------------------------------------------------------------- fonts

def load_ink(path):
    """Row-major flags, True where a pixel is ink: opaque (alpha >= 128) and luminance < 128 (dark ink on a
    transparent or white background). Luminance is 0.299 R + 0.587 G + 0.114 B in integers. Returns (w, h, flags)."""
    img = open_rgba(path)
    raw = img.tobytes()
    ink = [raw[i + 3] >= 128 and 299 * raw[i] + 587 * raw[i + 1] + 114 * raw[i + 2] < 128000
           for i in range(0, len(raw), 4)]
    return img.width, img.height, ink


def font_blob(path, fw, fh, chars, spacing=1):
    chars = chars.replace("\r", "").replace("\n", "")  # a newline is a line break for Canvas::text, never a glyph
    if not (1 <= fw <= 8 and 1 <= fh <= 255):
        raise ToolError("font cell must be 1..8 wide and 1..255 high")
    if not 0 <= spacing <= 255:
        raise ToolError("spacing must be 0..255")
    cps = [ord(c) for c in chars]
    if not cps:
        raise ToolError("no characters")
    if len(set(cps)) != len(cps):
        raise ToolError("duplicate characters")
    if max(cps) > 0xFFFF:
        raise ToolError("codepoint above U+FFFF")
    W, H, ink = load_ink(path)
    if W % fw or H % fh:
        raise ToolError(f"image {W}x{H} is not a multiple of cell {fw}x{fh}")
    cols, cells = W // fw, (W // fw) * (H // fh)
    if cells < len(cps):
        raise ToolError(f"image has {cells} cells for {len(cps)} characters")

    def cell(i):  # one byte per row, bit (fw - 1 - x) = pixel x
        x0, y0 = i % cols * fw, i // cols * fh
        return bytes(sum(ink[(y0 + y) * W + x0 + x] << (fw - 1 - x) for x in range(fw)) for y in range(fh))

    if any(any(cell(i)) for i in range(len(cps), cells)):
        raise ToolError("cells beyond the character list must be empty")
    order = sorted(range(len(cps)), key=lambda i: cps[i])
    cps, glyphs = [cps[i] for i in order], [cell(i) for i in order]  # glyph index = rank of the codepoint
    # Base block: the run of consecutive codepoints from the smallest one, if that is < 256, capped at codepoint 255 and
    # 255 glyphs. Otherwise no base block (first = 0, count = 0). Every other run becomes one range.
    base = 0
    if cps[0] < 256:
        while base < min(len(cps), 255) and cps[base] == cps[0] + base and cps[base] < 256:
            base += 1
    ranges, i = [], base
    while i < len(cps):
        j = i + 1
        while j < len(cps) and cps[j] == cps[j - 1] + 1:
            j += 1
        ranges.append((cps[i], j - i, i))
        i = j
    if len(ranges) > 255:
        raise ToolError("more than 255 ranges")
    payload = (struct.pack("<6B", fw, fh, cps[0] if base else 0, base, spacing, len(ranges))
               + b"".join(struct.pack("<3H", *r) for r in ranges) + b"".join(glyphs))
    return blob(T_FONT, payload)


# ---------------------------------------------------------------- tilemaps

def tilemap_blob(path, layer_name=None):
    try:
        with open(path, "r", encoding="utf-8") as f:
            text = f.read()
    except OSError as e:
        raise ToolError(f"cannot read {path}: {e}")
    if path.lower().endswith(".json"):
        w, h, ids = _tiled_ids(text, layer_name)
    else:
        w, h, ids = _csv_ids(text)
    if not (1 <= w <= 0xFFFF and 1 <= h <= 0xFFFF):
        raise ToolError("map size out of range")
    return blob(T_TILEMAP, struct.pack("<HH", w, h) + bytes(ids))


def _csv_ids(text):
    rows = []
    for line in text.splitlines():
        if not line.strip():
            continue
        try:
            row = [int(v) for v in line.split(",")]
        except ValueError:
            raise ToolError(f"CSV line {len(rows) + 1}: not a list of integers")
        rows.append([0 if v < 0 else v for v in row])  # old Tiled exporters write -1 for empty
    if not rows:
        raise ToolError("CSV is empty")
    if any(len(r) != len(rows[0]) for r in rows):
        raise ToolError("CSV rows have different lengths")
    ids = [v for r in rows for v in r]
    if max(ids) > 254:
        raise ToolError("CSV tile id > 254 (255 is reserved)")
    return len(rows[0]), len(rows), ids


def _tiled_ids(text, layer_name):
    try:
        m = json.loads(text)
        tilesets, layers = m["tilesets"], m["layers"]
    except (ValueError, KeyError, TypeError):
        raise ToolError("not a Tiled map JSON")
    if m.get("infinite"):
        raise ToolError("infinite maps not supported")
    if len(tilesets) != 1:
        raise ToolError("multiple tilesets not supported" if len(tilesets) > 1 else "map has no tileset")
    firstgid = tilesets[0].get("firstgid", 1)
    cands = [l for l in layers if l.get("type") == "tilelayer" and layer_name in (None, l.get("name"))]
    if not cands:
        raise ToolError("no matching tile layer")
    layer = cands[0]
    if "chunks" in layer:
        raise ToolError("layer chunks (infinite map) not supported")
    if "encoding" in layer or "compression" in layer:
        raise ToolError("encoded/compressed layer data not supported (export as plain CSV/JSON array)")
    data, w, h = layer.get("data"), layer.get("width"), layer.get("height")
    if not isinstance(data, list) or not all(type(g) is int for g in data):
        raise ToolError("layer.data must be a plain list of integers")
    if w is None or h is None or len(data) != w * h:
        raise ToolError("layer width*height does not match data length")
    ids = []
    for g in data:
        if g < 0 or g & FLIP_MASK:
            raise ToolError(f"gid {g:#x} has flip bits or is negative")
        if g == 0:
            ids.append(0)
        elif g < firstgid:
            raise ToolError(f"gid {g} is below tileset firstgid {firstgid}")
        else:
            tid = g - firstgid + 1
            if tid > 254:
                raise ToolError(f"tile id {tid} out of range 1..254")
            ids.append(tid)
    return w, h, ids


# ---------------------------------------------------------------- strings

def strings_blob(path, prefix="TXT_"):
    """UTF-8 CSV -> (text-table blob, ids). Header: id, then 1..16 language codes (2 letters a-z, the first one is the
    fallback). One row per string: id, then one cell per language; an empty cell in another language falls back to the
    first one. Blank rows and rows starting with # are skipped. Identical strings share one pool entry."""
    try:
        with open(path, "r", encoding="utf-8-sig", newline="") as f:  # -sig: a BOM is not part of the header
            rows = list(csv.reader(f))
    except (OSError, UnicodeDecodeError, csv.Error) as e:
        raise ToolError(f"cannot read {path}: {e}")
    rows = [(n, r) for n, r in enumerate(rows, 1) if any(r) and not r[0].startswith("#")]
    if not rows:
        raise ToolError("strings CSV has no header")
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", prefix + "COUNT"):
        raise ToolError(f"bad prefix {prefix!r}")
    if any("\0" in c for _, r in rows for c in r):
        raise ToolError("a cell contains a NUL character")
    head = rows[0][1]
    codes = head[1:]
    if head[0] != "id" or not codes:
        raise ToolError('header must be "id" followed by the language codes')
    if len(codes) > 16:
        raise ToolError("more than 16 languages")
    for c in codes:
        if not re.fullmatch(r"[a-z]{2}", c):
            raise ToolError(f"language code {c!r} is not 2 letters a-z")
    if len(set(codes)) != len(codes):
        raise ToolError("duplicate language code")
    body = rows[1:]
    if not body:
        raise ToolError("no strings")
    if len(body) > 0xFFFF:
        raise ToolError("more than 65535 strings")
    ids = []
    for n, r in body:
        if len(r) != len(head):
            raise ToolError(f"row {n}: {len(r)} cells, the header has {len(head)}")
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", r[0]) or r[0] == "COUNT":
            raise ToolError(f"row {n}: bad id {r[0]!r}")
        if r[0] in ids:
            raise ToolError(f"row {n}: duplicate id {r[0]}")
        if r[1] == "":
            raise ToolError(f"row {n}: empty text in the first language ({codes[0]})")
        ids.append(r[0])
    pool, where, offs = bytearray(), {}, []
    for li in range(len(codes)):  # language-major, id order: the first occurrence of a text owns the pool entry
        for _, r in body:
            if r[1 + li] == "":
                offs.append(0xFFFFFFFF)
                continue
            s = r[1 + li].encode("utf-8") + b"\0"
            if s not in where:
                where[s] = len(pool)
                pool += s
            offs.append(where[s])
    if len(pool) > 0xFFFFFFFE:
        raise ToolError("string pool too large")
    payload = (struct.pack("<BBH", len(codes), 0, len(ids)) + "".join(codes).encode("ascii")
               + struct.pack(f"<{len(offs)}I", *offs) + bytes(pool))
    return blob(T_STRINGS, payload), ids


def strings_enum(prefix, ids):
    names = [f"    {prefix}{s} = {i},\n" for i, s in enumerate(ids)] + [f"    {prefix}COUNT = {len(ids)},\n"]
    return "\nenum : unsigned short {\n" + "".join(names) + "};"


# ---------------------------------------------------------------- output

def default_name(path):
    stem = os.path.splitext(os.path.basename(path))[0]
    return "k" + "".join(p[:1].upper() + p[1:] for p in re.split(r"[^0-9A-Za-z]+", stem) if p)


def _lines(items, fmt):
    cells = [fmt % v for v in items]
    return "".join("    " + ", ".join(cells[i:i + 12]) + ",\n" for i in range(0, len(cells), 12))


def _write_c(path, decl, name, items, fmt, tail):
    text = ("// generated by tools/pge_assets.py - do not edit\n#pragma once\n"
            f"{decl} {name}[] = {{\n{_lines(items, fmt)}}};\n{tail}\n")
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write(text)


def write_c(path, data, name, extra=""):
    _write_c(path, "alignas(4) static const unsigned char", name, data, "0x%02X",
             f"static const unsigned int {name}_size = sizeof({name});{extra}")


def write_palette_c(path, palette, name):
    _write_c(path, "static const unsigned short", name, palette, "0x%04X",
             f"static const unsigned int {name}_count = sizeof({name}) / sizeof({name}[0]);")


def emit(data, args, extra=""):
    if args.out_bin:
        with open(args.out_bin, "wb") as f:
            f.write(data)
    if args.out_c:
        write_c(args.out_c, data, args.name or default_name(args.input), extra)
    if not (args.out_bin or args.out_c):
        print(f"{args.input}: {len(data)} bytes (payload {len(data) - 12})")


# ---------------------------------------------------------------- selftest

def _golden_png(path):
    t, r, g = (0, 0, 0, 0), (255, 0, 0, 255), (0, 255, 0, 255)
    img = Image.new("RGBA", (4, 2))
    img.putdata([t, r, g, g, r, t, t, r])
    img.save(path)


GOLDEN_SPRITES = bytes.fromhex(
    "50474541 0001 0000 1A000000  02000200 02000000 03000100  0000 00F8 E007  00010100 02020001")
GOLDEN_FONT = bytes.fromhex(
    "50474541 0201 0000 12000000  03 02 41 02 01 01  1604 0100 0200  0507 0205 0701")
GOLDEN_TILEMAP = bytes.fromhex("50474541 0601 0000 0A000000  0300 0200  00 01 02 03 00 FE")
# en, ru x ids A, B: offsets 0, 3, 6, missing; pool "Hi\0" "Yo\0" "\u0414\u0430\0"
GOLDEN_STRINGS = bytes.fromhex(
    "50474541 0701 0000 23000000  02 00 0200  656E 7275  00000000 03000000 06000000 FFFFFFFF  486900 596F00 D094D0B000")


def _golden_font_png(path, order=(0, 1, 2), tail=""):
    """Three 3x2 cells (ink bits 5 7 | 2 5 | 7 1) in the given order, plus an optional extra cell. K, D (luminance 127), S
    (alpha 128) are ink; '.', W, L (luminance 128), T (alpha 127) are not."""
    px = {"K": (0, 0, 0, 255), "D": (127, 127, 127, 255), "S": (0, 0, 0, 128), ".": (0, 0, 0, 0),
          "W": (255, 255, 255, 255), "L": (128, 128, 128, 255), "T": (0, 0, 0, 127)}
    cells = [("KLDKKK", ".KWDTK", "KDSWTS")[i] for i in order] + ([tail] if tail else [])
    img = Image.new("RGBA", (3 * len(cells), 2))
    img.putdata([px[c[y * 3 + x]] for y in range(2) for c in cells for x in range(3)])
    img.save(path)


def selftest(emit_dir):
    def check(cond, msg):
        if not cond:
            raise AssertionError(msg)

    def rejects(label, fn):
        try:
            fn()
        except ToolError:
            return
        raise AssertionError(f"not rejected: {label}")

    with tempfile.TemporaryDirectory() as d:
        p = lambda n: os.path.join(d, n)
        png = p("g.png")
        _golden_png(png)
        b = sprites_blob(png, 2, 2)
        check(b == GOLDEN_SPRITES, f"golden sprites mismatch: {b.hex(' ')}")
        check(sprites_blob(png, 2, 2) == b, "conversion is not deterministic")
        check(palette_list(png) == [0x0000, 0xF800, 0x07E0], "golden palette mismatch")

        fpng = p("f.png")
        _golden_font_png(fpng)
        fb = font_blob(fpng, 3, 2, "AB\u0416")
        check(fb == GOLDEN_FONT, f"golden font mismatch: {fb.hex(' ')}")
        check(font_blob(fpng, 3, 2, "AB\u0416") == fb, "font conversion is not deterministic")
        _golden_font_png(p("f_rev.png"), order=(2, 0, 1))
        check(font_blob(p("f_rev.png"), 3, 2, "\u0416AB") == fb, "glyphs are not sorted by codepoint")
        _golden_font_png(p("f_tail.png"), tail="......")
        check(font_blob(p("f_tail.png"), 3, 2, "AB\u0416\n") == fb, "empty trailing cell / newline changed the blob")
        Image.new("RGBA", (287, 1), (0, 0, 0, 255)).save(p("f287.png"))
        b287 = font_blob(p("f287.png"), 1, 1, "".join(map(chr, range(14, 301))))
        check(b287[12:18] == bytes([1, 1, 14, 242, 1, 1]) and b287[18:24] == struct.pack("<3H", 256, 45, 242),
              "base block must stop at codepoint 255")
        check(font_blob(fpng, 3, 2, "\u0416\u0417\u0418")[14:18] == bytes([0, 0, 1, 1]), "no base block expected")

        def sparse(n):  # n one-pixel glyphs at pairwise non-consecutive codepoints: n ranges
            Image.new("RGBA", (n, 1), (0, 0, 0, 255)).save(p("sp.png"))
            return font_blob(p("sp.png"), 1, 1, "".join(chr(0x100 + 2 * i) for i in range(n)))

        check(sparse(255)[17] == 255, "255 ranges must be accepted")

        def wtext(name, text, bom=False):  # always UTF-8 (open(..., "w") would use the Windows code page)
            with open(p(name), "wb") as f:
                f.write((b"\xef\xbb\xbf" if bom else b"") + text.encode("utf-8"))
            return p(name)

        s_ok = 'id,en,ru\nA,"Hi","\u0414\u0430"\nB,"Yo",""\n'
        sb, sids = strings_blob(wtext("s.csv", s_ok))
        check(sb == GOLDEN_STRINGS and sids == ["A", "B"], f"golden strings mismatch: {sb.hex(' ')}")
        check(strings_blob(p("s.csv"))[0] == sb, "strings conversion is not deterministic")
        check(strings_blob(wtext("s_bom.csv", s_ok, bom=True))[0] == sb, "a BOM changed the strings blob")
        check(strings_blob(wtext("s_cr.csv", "# c\r\nid,en,ru\r\n\r\nA,Hi,\u0414\u0430\r\n# x\r\nB,Yo,\r\n"))[0] == sb,
              "comments / blank rows / CRLF changed the strings blob")
        check(len(strings_blob(wtext("s_dd.csv", "id,en,de\nA,OK,OK\n"))[0]) == 31, "identical strings are not shared")
        sbad = lambda text: lambda: strings_blob(wtext("s_bad.csv", text))
        rejects("strings duplicate id", sbad("id,en\nA,x\nA,y\n"))
        rejects("strings empty fallback", sbad("id,en,ru\nA,,x\n"))
        rejects("strings code EN", sbad("id,EN\nA,x\n"))
        rejects("strings duplicate code", sbad("id,en,en\nA,x,y\n"))
        langs = ",".join("a" + chr(97 + i) for i in range(17))
        rejects("strings 17 languages", sbad("id," + langs + "\nA," + "x," * 16 + "x\n"))
        rejects("strings ragged row", sbad("id,en,ru\nA,x\n"))
        rejects("strings id 1x", sbad("id,en\n1x,x\n"))
        rejects("strings no rows", sbad("id,en\n"))
        rejects("strings no header", sbad("A,x\n"))
        rejects("strings id only", sbad("id\nA\n"))
        rejects("strings NUL", sbad("id,en\nA,a\0b\n"))
        with open(p("s_bad8.csv"), "wb") as f:
            f.write(b"id,en\nA,\xff\n")
        rejects("strings bad UTF-8", lambda: strings_blob(p("s_bad8.csv")))

        csv, js = p("m.csv"), p("m.json")
        with open(csv, "w") as f:
            f.write("0,1,2\n3,0,254\n")
        with open(js, "w") as f:
            json.dump(_tiled([0, 1, 2, 3, 0, 254]), f)
        for src in (csv, js):
            tb = tilemap_blob(src)
            check(tb == GOLDEN_TILEMAP, f"golden tilemap mismatch ({src}): {tb.hex(' ')}")

        def write(name, text):
            with open(p(name), "w") as f:
                f.write(text)
            return p(name)

        def jmap(**kw):
            m = _tiled([0, 1, 2, 3, 0, 254])
            lay = m["layers"][0]
            for k, v in kw.items():
                (m if k in ("infinite", "tilesets") else lay)[k] = v
            return write("x.json", json.dumps(m))

        write("neg.csv", "0,-1\n-1,2\n")
        check(tilemap_blob(p("neg.csv"))[12:] == bytes([2, 0, 2, 0, 0, 0, 0, 2]), "negative CSV id not mapped to 0")

        big = Image.new("RGBA", (257, 1))
        big.putdata([((i % 32) * 8, (i // 32) * 8, 0, 255) for i in range(257)])
        big.save(p("big.png"))
        rejects("257 colours", lambda: sprites_blob(p("big.png"), 1, 1))
        rejects("size not multiple", lambda: sprites_blob(png, 3, 2))
        rejects("font duplicate char", lambda: font_blob(fpng, 3, 2, "AA\u0416"))
        rejects("font width 9", lambda: font_blob(fpng, 9, 2, "A"))
        rejects("font height 0", lambda: font_blob(fpng, 3, 0, "A"))
        rejects("font spacing 256", lambda: font_blob(fpng, 3, 2, "A", 256))
        rejects("font too few cells", lambda: font_blob(fpng, 3, 2, "AB\u0416!"))
        rejects("font cell mismatch", lambda: font_blob(fpng, 4, 2, "A"))
        rejects("font codepoint > FFFF", lambda: font_blob(fpng, 3, 2, "A\U0001F600"))
        rejects("font no chars", lambda: font_blob(fpng, 3, 2, "\n"))
        _golden_font_png(p("f_ink.png"), tail="K.....")
        rejects("font non-empty extra cell", lambda: font_blob(p("f_ink.png"), 3, 2, "AB\u0416"))
        rejects("font 256 ranges", lambda: sparse(256))
        rejects("csv 255", lambda: tilemap_blob(write("a.csv", "0,255\n")))
        rejects("csv ragged", lambda: tilemap_blob(write("b.csv", "0,1\n2\n")))
        rejects("base64", lambda: tilemap_blob(jmap(encoding="base64")))
        rejects("2 tilesets", lambda: tilemap_blob(jmap(tilesets=[{"firstgid": 1}, {"firstgid": 9}])))
        rejects("flip bit", lambda: tilemap_blob(jmap(data=[0x80000001, 0, 0, 0, 0, 0])))
        rejects("infinite", lambda: tilemap_blob(jmap(infinite=True)))
        rejects("gid < firstgid", lambda: tilemap_blob(jmap(tilesets=[{"firstgid": 5}])))

        if emit_dir:
            os.makedirs(emit_dir, exist_ok=True)
            write_c(os.path.join(emit_dir, "sample_sprites.h"), b, "kSampleSprites")
            write_c(os.path.join(emit_dir, "sample_font.h"), fb, "kSampleFont")
            write_c(os.path.join(emit_dir, "sample_tilemap.h"), tilemap_blob(csv), "kSampleTilemap")
            write_c(os.path.join(emit_dir, "sample_strings.h"), sb, "kSampleStrings")
    print("selftest OK")


def _tiled(data):
    return {"tilesets": [{"firstgid": 1}],
            "layers": [{"type": "tilelayer", "name": "L", "width": 3, "height": 2, "data": data}]}


# ---------------------------------------------------------------- CLI

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(sp, kind):
        sp.add_argument("input")
        sp.add_argument("--out-c")
        sp.add_argument("--name")
        if kind != "palette":
            sp.add_argument("--out-bin")

    sp = sub.add_parser("sprites")
    common(sp, "sprites")
    sp.add_argument("--frame", required=True, help="WxH")
    sp.add_argument("--atlas", action="store_true")
    sp.add_argument("--type", choices=["sprites", "tileset"], default="sprites")
    sp = sub.add_parser("font", description="Font blob from a PNG grid of WxH cells read row-major; cell i is the glyph of "
                        "character i. A pixel is ink when opaque (alpha >= 128) and luminance < 128.")
    common(sp, "font")
    sp.add_argument("--frame", required=True, help="WxH cell size, W 1..8, H 1..255")
    g = sp.add_mutually_exclusive_group(required=True)
    g.add_argument("--chars", help="characters of the cells in cell order (UTF-8)")
    g.add_argument("--chars-file", help="UTF-8 file holding the characters (newlines ignored)")
    sp.add_argument("--spacing", type=int, default=1, help="pixels between glyphs (default 1)")
    sp = sub.add_parser("strings", description="Text-table blob from a UTF-8 CSV: header id,<lang codes>; one row per "
                        "string. With --out-c the header also gets an enum <PREFIX><ID> = 0.. and <PREFIX>COUNT.")
    common(sp, "strings")
    sp.add_argument("--prefix", default="TXT_", help="enum name prefix (default TXT_)")
    sp = sub.add_parser("palette")
    common(sp, "palette")
    sp = sub.add_parser("tilemap")
    common(sp, "tilemap")
    sp.add_argument("--layer")
    sp = sub.add_parser("selftest")
    sp.add_argument("--emit-dir")

    args = ap.parse_args(argv)
    try:
        if args.cmd == "selftest":
            selftest(args.emit_dir)
        elif args.cmd in ("sprites", "font"):
            m = re.fullmatch(r"(\d+)x(\d+)", args.frame)
            if not m:
                raise ToolError("--frame must look like 16x16")
            if args.cmd == "sprites":
                emit(sprites_blob(args.input, int(m[1]), int(m[2]), args.atlas, args.type), args)
            else:
                chars = args.chars
                if chars is None:
                    try:
                        with open(args.chars_file, "r", encoding="utf-8-sig") as f:  # -sig: a BOM is not a character
                            chars = f.read()
                    except (OSError, ValueError) as e:
                        raise ToolError(f"cannot read {args.chars_file}: {e}")
                emit(font_blob(args.input, int(m[1]), int(m[2]), chars, args.spacing), args)
        elif args.cmd == "tilemap":
            emit(tilemap_blob(args.input, args.layer), args)
        elif args.cmd == "strings":
            data, ids = strings_blob(args.input, args.prefix)
            emit(data, args, strings_enum(args.prefix, ids))
        else:
            pal = palette_list(args.input)
            if args.out_c:
                write_palette_c(args.out_c, pal, args.name or "kPal")
            else:
                print(" ".join(f"0x{v:04X}" for v in pal))
    except ToolError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except AssertionError as e:
        print(f"selftest FAILED: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
