#!/usr/bin/env python3
"""Asset converter for the pixel engine: PNG -> sprite/tileset blob, palette, Tiled/CSV -> tilemap blob.

Blob = 12-byte header ("PGEA", type u8, version u8 = 1, u16 0, u32 payload size) + payload, little-endian.
"""
import argparse
import json
import os
import re
import struct
import sys
import tempfile

from PIL import Image

T_SPRITES, T_TILESET, T_TILEMAP = 0, 1, 6
FLIP_MASK = 0xF0000000  # Tiled flip/rotate flag bits (plus a spare nibble bit)


class ToolError(Exception):
    pass


def blob(typ, payload):
    return b"PGEA" + struct.pack("<BBHI", typ, 1, 0, len(payload)) + payload


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


# ---------------------------------------------------------------- images

def load_pixels(path):
    """Row-major RGB565 values, None where alpha < 128. Returns (w, h, pixels)."""
    try:
        img = Image.open(path).convert("RGBA")
    except OSError as e:
        raise ToolError(f"cannot read image {path}: {e}")
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


def write_c(path, data, name):
    _write_c(path, "alignas(4) static const unsigned char", name, data, "0x%02X",
             f"static const unsigned int {name}_size = sizeof({name});")


def write_palette_c(path, palette, name):
    _write_c(path, "static const unsigned short", name, palette, "0x%04X",
             f"static const unsigned int {name}_count = sizeof({name}) / sizeof({name}[0]);")


def emit(data, args):
    if args.out_bin:
        with open(args.out_bin, "wb") as f:
            f.write(data)
    if args.out_c:
        write_c(args.out_c, data, args.name or default_name(args.input))
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
GOLDEN_TILEMAP = bytes.fromhex("50474541 0601 0000 0A000000  0300 0200  00 01 02 03 00 FE")


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
            write_c(os.path.join(emit_dir, "sample_tilemap.h"), tilemap_blob(csv), "kSampleTilemap")
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
        elif args.cmd == "sprites":
            m = re.fullmatch(r"(\d+)x(\d+)", args.frame)
            if not m:
                raise ToolError("--frame must look like 16x16")
            emit(sprites_blob(args.input, int(m[1]), int(m[2]), args.atlas, args.type), args)
        elif args.cmd == "tilemap":
            emit(tilemap_blob(args.input, args.layer), args)
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
