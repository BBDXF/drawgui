#!/usr/bin/env python3
"""Generate the tiny TrueType fonts the font-fallback tests resolve against.

Why this exists
---------------
The fallback resolver's answer is a function of the fonts it was given. Asserting
anything about the fonts of the machine running the suite therefore produces a
test that is green here and red on a CI image with a different font package set,
and the usual escape - assert only that the call returned something - passes just
as happily on a tofu box.

So the tests get their own font directory, four fonts, built from this file. The
coverage of each is written here and is the same on every machine, which is what
lets a test pin an exact family name and an exact glyph id rather than a
non-null pointer.

Nothing here is a general font toolkit. It emits the ten tables FreeType needs to
open a file and report a family name, a cmap and an outline, and no more.

Determinism: the timestamps are fixed and every table is written from sorted
input, so two runs produce byte-identical files.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

# 1904-01-01 based LONGDATETIME, pinned so the output is byte-stable.
FIXED_DATE = 3_000_000_000

UNITS_PER_EM = 1000
ASCENDER = 800
DESCENDER = -200
ADVANCE = 600


class Glyph:
    """One outline: a list of closed contours of on-curve points."""

    def __init__(self, contours: list[list[tuple[int, int]]]) -> None:
        self.contours = contours

    @property
    def is_empty(self) -> bool:
        return not self.contours

    def bounds(self) -> tuple[int, int, int, int]:
        if self.is_empty:
            return (0, 0, 0, 0)
        xs = [p[0] for c in self.contours for p in c]
        ys = [p[1] for c in self.contours for p in c]
        return (min(xs), min(ys), max(xs), max(ys))


def rect(x0: int, y0: int, x1: int, y1: int) -> list[tuple[int, int]]:
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def glyf_entry(glyph: Glyph) -> bytes:
    """One simple-glyph record. Empty glyphs contribute zero bytes, which is how
    loca expresses 'this glyph has no outline'."""
    if glyph.is_empty:
        return b""
    x_min, y_min, x_max, y_max = glyph.bounds()
    out = struct.pack(">hhhhh", len(glyph.contours), x_min, y_min, x_max, y_max)

    end_pts: list[int] = []
    running = -1
    for contour in glyph.contours:
        running += len(contour)
        end_pts.append(running)
    out += b"".join(struct.pack(">H", e) for e in end_pts)
    out += struct.pack(">H", 0)  # instructionLength

    points = [p for c in glyph.contours for p in c]
    # 0x01 = ON_CURVE_POINT, and no short/same bits, so every delta is a plain
    # int16 pair. Larger than the compressed form and much easier to be right.
    out += bytes([0x01] * len(points))

    prev_x = 0
    for x, _ in points:
        out += struct.pack(">h", x - prev_x)
        prev_x = x
    prev_y = 0
    for _, y in points:
        out += struct.pack(">h", y - prev_y)
        prev_y = y

    while len(out) % 4 != 0:
        out += b"\0"
    return out


def build_cmap_format4(cmap: dict[int, int]) -> bytes:
    bmp = sorted(cp for cp in cmap if cp <= 0xFFFF)
    segments: list[tuple[int, int, int]] = []  # (start, end, start_gid)
    for cp in bmp:
        gid = cmap[cp]
        if segments and cp == segments[-1][1] + 1 and gid == segments[-1][2] + (
            segments[-1][1] + 1 - segments[-1][0]
        ):
            segments[-1] = (segments[-1][0], cp, segments[-1][2])
        else:
            segments.append((cp, cp, gid))
    segments.append((0xFFFF, 0xFFFF, 0))  # required terminator

    seg_count = len(segments)
    search_range = 2
    entry_selector = 0
    while search_range * 2 <= seg_count * 2:
        search_range *= 2
        entry_selector += 1
    search_range //= 1
    range_shift = seg_count * 2 - search_range

    body = struct.pack(">HHHH", seg_count * 2, search_range, entry_selector, range_shift)
    body += b"".join(struct.pack(">H", s[1]) for s in segments)
    body += struct.pack(">H", 0)  # reservedPad
    body += b"".join(struct.pack(">H", s[0]) for s in segments)
    for start, end, start_gid in segments:
        delta = 0 if end == 0xFFFF and start == 0xFFFF and start_gid == 0 else (
            (start_gid - start) & 0xFFFF
        )
        body += struct.pack(">H", delta)
    body += b"".join(struct.pack(">H", 0) for _ in segments)  # idRangeOffset

    length = 2 + 2 + 2 + len(body)
    return struct.pack(">HHH", 4, length, 0) + body


def build_cmap_format12(cmap: dict[int, int]) -> bytes:
    groups: list[tuple[int, int, int]] = []
    for cp in sorted(cmap):
        gid = cmap[cp]
        if groups and cp == groups[-1][1] + 1 and gid == groups[-1][2] + (
            groups[-1][1] + 1 - groups[-1][0]
        ):
            groups[-1] = (groups[-1][0], cp, groups[-1][2])
        else:
            groups.append((cp, cp, gid))
    body = b"".join(struct.pack(">III", g[0], g[1], g[2]) for g in groups)
    length = 16 + len(body)
    return struct.pack(">HHIII", 12, 0, length, 0, len(groups)) + body


def build_cmap(cmap: dict[int, int]) -> bytes:
    sub4 = build_cmap_format4(cmap)
    sub12 = build_cmap_format12(cmap)
    header_size = 4 + 8 * 2
    off4 = header_size
    off12 = off4 + len(sub4)
    out = struct.pack(">HH", 0, 2)
    out += struct.pack(">HHI", 3, 1, off4)
    out += struct.pack(">HHI", 3, 10, off12)
    return out + sub4 + sub12


def build_name(family: str, subfamily: str, postscript: str) -> bytes:
    entries = [(1, family), (2, subfamily), (3, f"drawgui-test:{postscript}"),
               (4, f"{family} {subfamily}"), (6, postscript)]
    records: list[bytes] = []
    storage = b""
    for name_id, text in entries:
        # Windows / UCS-2 / en-US, which is what every scanner in this project
        # reads. A Macintosh record is deliberately not emitted: FreeType
        # prefers the Windows one anyway, and two copies can disagree.
        encoded = text.encode("utf-16-be")
        records.append(struct.pack(">HHHHHH", 3, 1, 0x0409, name_id, len(encoded),
                                   len(storage)))
        storage += encoded
    count = len(records)
    string_offset = 6 + 12 * count
    return struct.pack(">HHH", 0, count, string_offset) + b"".join(records) + storage


def build_os2(cmap: dict[int, int], weight: int, codepages: int) -> bytes:
    # Both fields are uint16, so a font whose whole coverage is astral reports
    # the 0xFFFF sentinel the spec reserves for exactly that.
    bmp = [cp for cp in cmap if cp <= 0xFFFF]
    first = min(bmp) if bmp else 0xFFFF
    last = max(bmp) if bmp else 0xFFFF
    out = struct.pack(">HhHHH", 4, ADVANCE, weight, 5, 0)
    out += struct.pack(">hhhhhhhhhh", 650, 700, 0, 0, 650, 700, 0, 480, 50, 250)
    out += struct.pack(">h", 0)  # sFamilyClass
    out += bytes(10)  # panose
    out += struct.pack(">IIII", 0, 0, 0, 0)  # ulUnicodeRange 1..4
    out += b"DGUI"
    out += struct.pack(">HHH", 0x0040, first, last)
    out += struct.pack(">hhh", ASCENDER, DESCENDER, 0)
    out += struct.pack(">HH", ASCENDER, -DESCENDER)
    out += struct.pack(">II", codepages, 0)
    out += struct.pack(">hh", 500, 700)
    out += struct.pack(">HHH", 0, 32, 1)
    assert len(out) == 96, len(out)
    return out


def build_colr() -> bytes:
    """A minimal COLR v0: glyph 1 is drawn as one layer, itself, in palette 0.

    The table exists so a test font can be DETECTED as a colour font. drawgui
    reads CBDT/sbix/COLR presence off the font rather than matching a family
    name, so proving that rule needs a font that carries one of them.
    """
    header_size = 14
    base_records = struct.pack(">HHH", 1, 0, 1)
    layer_records = struct.pack(">HH", 1, 0)
    return (struct.pack(">HHIIH", 0, 1, header_size, header_size + len(base_records), 1)
            + base_records + layer_records)


def build_cpal() -> bytes:
    """One palette of one opaque colour, which is all COLR v0 above refers to."""
    header_size = 12 + 2
    return (struct.pack(">HHHHI", 0, 1, 1, 1, header_size) + struct.pack(">H", 0)
            + bytes([0x30, 0x60, 0xE0, 0xFF]))


def build_post() -> bytes:
    return struct.pack(">IihhIIIII", 0x00030000, 0, -100, 50, 0, 0, 0, 0, 0)


def table_checksum(data: bytes) -> int:
    padded = data + b"\0" * ((4 - len(data) % 4) % 4)
    total = 0
    for i in range(0, len(padded), 4):
        total = (total + struct.unpack(">I", padded[i:i + 4])[0]) & 0xFFFFFFFF
    return total


def assemble(tables: dict[str, bytes]) -> bytes:
    tags = sorted(tables)
    num_tables = len(tags)
    search_range = 1
    entry_selector = 0
    while search_range * 2 <= num_tables:
        search_range *= 2
        entry_selector += 1
    search_range *= 16
    range_shift = num_tables * 16 - search_range

    header = struct.pack(">IHHHH", 0x00010000, num_tables, search_range, entry_selector,
                         range_shift)
    offset = len(header) + 16 * num_tables
    directory = b""
    body = b""
    for tag in tags:
        data = tables[tag]
        directory += struct.pack(">4sIII", tag.encode("ascii"), table_checksum(data), offset,
                                 len(data))
        padded = data + b"\0" * ((4 - len(data) % 4) % 4)
        body += padded
        offset += len(padded)

    font = header + directory + body
    # head.checkSumAdjustment is computed over the whole file with that field
    # zeroed, which is why head is written with a zero there and patched here.
    head_offset = None
    for i, tag in enumerate(tags):
        if tag == "head":
            head_offset = struct.unpack(">I", font[len(header) + 16 * i + 8:
                                                    len(header) + 16 * i + 12])[0]
    assert head_offset is not None
    adjustment = (0xB1B0AFBA - table_checksum(font)) & 0xFFFFFFFF
    font = (font[:head_offset + 8] + struct.pack(">I", adjustment)
            + font[head_offset + 12:])
    return font


def build_font(spec: "FontSpec") -> bytes:
    # Glyph 0 is .notdef and is given a real outline on purpose: a missing glyph
    # must be visible, and a test that renders one needs it to put ink down.
    glyphs = [Glyph([rect(60, 0, 540, 700), rect(140, 80, 460, 620)])]
    cmap: dict[int, int] = {}
    for codepoint in sorted(spec.codepoints):
        cmap[codepoint] = len(glyphs)
        glyphs.append(Glyph([spec.shape]))

    glyf = b""
    loca = [0]
    for glyph in glyphs:
        glyf += glyf_entry(glyph)
        loca.append(len(glyf))

    xs = [g.bounds() for g in glyphs if not g.is_empty]
    x_min = min(b[0] for b in xs)
    y_min = min(b[1] for b in xs)
    x_max = max(b[2] for b in xs)
    y_max = max(b[3] for b in xs)

    head = struct.pack(">IIIIHHqqhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0x000B,
                       UNITS_PER_EM, FIXED_DATE, FIXED_DATE, x_min, y_min, x_max, y_max,
                       0, 8, 2, 1, 0)
    assert len(head) == 54, len(head)

    hhea = struct.pack(">IhhhHhhhhhhhhhhhh", 0x00010000, ASCENDER, DESCENDER, 0, ADVANCE,
                       0, 0, x_max, 1, 0, 0, 0, 0, 0, 0, 0, len(glyphs))
    assert len(hhea) == 36, len(hhea)

    maxp = struct.pack(">IHHHHHHHHHHHHHH", 0x00010000, len(glyphs), 16, 2, 0, 0, 2, 0, 0,
                       0, 0, 0, 0, 0, 0)
    assert len(maxp) == 32, len(maxp)

    hmtx = b"".join(struct.pack(">Hh", ADVANCE, 0) for _ in glyphs)

    tables = {
        "OS/2": build_os2(cmap, spec.weight, spec.codepages),
        "cmap": build_cmap(cmap),
        "glyf": glyf,
        "head": head,
        "hhea": hhea,
        "hmtx": hmtx,
        "loca": b"".join(struct.pack(">I", o) for o in loca),
        "maxp": maxp,
        "name": build_name(spec.family, "Regular", spec.postscript),
        "post": build_post(),
    }
    if spec.colour:
        tables["COLR"] = build_colr()
        tables["CPAL"] = build_cpal()
    return assemble(tables)


class FontSpec:
    def __init__(self, filename: str, family: str, postscript: str,
                 codepoints: list[int], shape: list[tuple[int, int]],
                 weight: int = 400, codepages: int = 0, colour: bool = False) -> None:
        self.filename = filename
        self.family = family
        self.postscript = postscript
        self.codepoints = codepoints
        self.shape = shape
        self.weight = weight
        self.codepages = codepages
        self.colour = colour


# The four fonts, and the whole point of each.
#
# Coverage is deliberately disjoint except for the two Han fonts, which cover
# EXACTLY the same codepoints with DIFFERENT outlines - that pair is what makes
# the Han-unification assertion falsifiable: if the language tag stopped
# selecting the chain, both languages would render the same rectangle.
SPECS = [
    FontSpec(
        filename="DgTestLatin.ttf",
        family="DgTest Latin",
        postscript="DgTestLatin",
        # ASCII printable, plus a Greek letter so "the primary already covers it"
        # can be tested with something that is not Latin, plus U+1F600 - which
        # is the DejaVu Sans trap in miniature. DejaVu carries a MONOCHROME
        # outline for that emoji, so a chain where the named family always wins
        # would answer every emoji with line art on a machine that has a colour
        # font. DgTest Colour below is what must beat this entry.
        codepoints=list(range(0x20, 0x7F)) + [0x03B1, 0x1F600],
        shape=rect(200, 0, 400, 700),
        codepages=0x00000001,  # Latin-1
    ),
    FontSpec(
        filename="DgTestHanHans.ttf",
        family="DgTest Han Hans",
        postscript="DgTestHanHans",
        codepoints=[0x4E2D, 0x6D77, 0x76F4, 0x9AA8],
        shape=rect(60, 0, 540, 240),  # a LOW wide bar, sitting on the baseline
        codepages=0x00040000,  # bit 18: 936 simplified Chinese
    ),
    FontSpec(
        filename="DgTestHanJa.ttf",
        family="DgTest Han Ja",
        postscript="DgTestHanJa",
        codepoints=[0x4E2D, 0x6D77, 0x76F4, 0x9AA8],
        shape=rect(60, 460, 540, 700),  # a HIGH wide bar - same codepoints, same area
        codepages=0x00020000,  # bit 17: 932 Japanese
    ),
    FontSpec(
        filename="DgTestRare.ttf",
        family="DgTest Rare",
        postscript="DgTestRare",
        # A BMP codepoint nothing else here has, and an astral one, so the
        # surrogate-free UTF-8 path and the cmap format 12 subtable are both
        # exercised by something. U+1F900 is inside the emoji-preferred range
        # and NO colour font here covers it, which is what exercises the
        # fall-through out of the colour-first step.
        codepoints=[0x16A0, 0x1F900],
        shape=rect(120, 100, 480, 500),
    ),
    FontSpec(
        filename="DgTestColour.ttf",
        family="DgTest Colour",
        postscript="DgTestColour",
        codepoints=[0x1F600],
        shape=rect(100, 100, 500, 600),
        colour=True,
    ),
]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="directory to write the fonts into")
    args = parser.parse_args()

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for spec in SPECS:
        data = build_font(spec)
        (out / spec.filename).write_bytes(data)
        print(f"{spec.filename}: {len(data)} bytes, {len(spec.codepoints)} codepoints")
    return 0


if __name__ == "__main__":
    sys.exit(main())
