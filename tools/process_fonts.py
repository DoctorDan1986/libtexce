#!/usr/bin/env python3
"""Build archived TXF3 AppVars: python3 tools/process_fonts.py [--outdir assets].

Host requirements: Pillow and fontTools. No runtime OpenType engine or cache.
Unmodified sources: CTAN fonts/lm/fonts/opentype/public/lm/lmroman10-regular.otf
(Roman 2.004) and fonts/lm-math/opentype/latinmodern-math.otf (Math 1.959).
Both use the GUST Font License (LPPL 1.3c or later); see tools/fonts licenses
and upstream README files. The renamed TeXFonts/TeXScrpt subsets are derived
bitmap data, not replacement OpenType fonts.

TXF3: eight-byte header (magic, normal asc/desc, metric count, zero), signed
LE16 metrics in TexFontMetric order, 312 nine-byte records (<HBBBbbbb: absolute
bitmap offset, advance, width, height, x/y bearing, italic correction, top
accent), then contiguous row-major 2-bit pixels, high bits first, no row
padding. Missing entries are zero; space has only advance. Normal line bounds
come from OS/2 typographic metrics, independent of structural/display ink.
Slots 256..307 are italic Latin, 308 math minus, 309..311 display operators.
"""

import argparse
from io import BytesIO
from pathlib import Path
import struct

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT_DIR = Path(__file__).resolve().parent.parent
FONT_DIR = Path(__file__).resolve().parent / "fonts"
GLYPH_COUNT = 312

# Keep in the same order as TexFontMetric. None denotes the raster em itself.
METRICS = [
    None,
    "AxisHeight",
    "SuperscriptShiftUp",
    "SuperscriptShiftUpCramped",
    "SuperscriptBottomMin",
    "SuperscriptBaselineDropMax",
    "SubscriptShiftDown",
    "SubscriptTopMax",
    "SubscriptBaselineDropMin",
    "SubSuperscriptGapMin",
    "SuperscriptBottomMaxWithSubscript",
    "SpaceAfterScript",
    "UpperLimitGapMin",
    "UpperLimitBaselineRiseMin",
    "LowerLimitGapMin",
    "LowerLimitBaselineDropMin",
    "FractionNumeratorShiftUp",
    "FractionDenominatorShiftDown",
    "FractionNumeratorDisplayStyleShiftUp",
    "FractionDenominatorDisplayStyleShiftDown",
    "FractionNumeratorGapMin",
    "FractionDenominatorGapMin",
    "FractionNumDisplayStyleGapMin",
    "FractionDenomDisplayStyleGapMin",
    "FractionRuleThickness",
    "RadicalVerticalGap",
    "RadicalDisplayStyleVerticalGap",
    "RadicalRuleThickness",
    "RadicalExtraAscender",
    "RadicalKernBeforeDegree",
    "RadicalKernAfterDegree",
    "RadicalDegreeBottomRaisePercent",
]
HEADER_SIZE = 8 + 2 * len(METRICS)
BITMAP_START = HEADER_SIZE + GLYPH_COUNT * 9

# Low-Index Math Symbols (0x01 - 0x10); byte positions are part of the API.
LOW_SYMBOLS = [
    0x222A,  # ∪ Union
    0x2229,  # ∩ Intersection
    0x2209,  # ∉ Not element of (notin)
    0x2205,  # ∅ Empty set
    0x2200,  # ∀ For all
    0x2203,  # ∃ Exists
    0x2286,  # ⊆ Subset or equal
    0x2261,  # ≡ Equivalent (identical to)
    0x223C,  # ∼ Similar (tilde operator)
    0x2245,  # ≅ Congruent (approximately equal)
    0x221D,  # ∝ Proportional to
    0x22A5,  # ⊥ Perpendicular (up tack)
    0x2225,  # ∥ Parallel to
    0x2220,  # ∠ Angle
    0x2218,  # ∘ Ring operator (composition)
    0x2295,  # ⊕ Circled plus (oplus)
]

# The Master Symbol List (0x80 - 0xBC), unchanged from the bitmap fonts.
SYMBOLS = [
    # Greek Lower
    0x03B1, 0x03B2, 0x03B3, 0x03B4, 0x03B5, 0x03B6, 0x03B7, 0x03B8,
    0x03B9, 0x03BA, 0x03BB, 0x03BC, 0x03BD, 0x03BE, 0x03BF, 0x03C0,
    0x03C1, 0x03C3, 0x03C4, 0x03C5, 0x03C6, 0x03C7, 0x03C8, 0x03C9,
    # Greek Upper
    0x0393, 0x0394, 0x0398, 0x039B, 0x039E, 0x03A0, 0x03A3, 0x03A6, 0x03A8, 0x03A9,
    # Calculus
    0x2202, 0x221E, 0x2207, 0x2032, 0x2113, 0x210F, 0x00B0,
    # Big Ops
    0x222B, 0x2211, 0x220F,
    # Operators
    0x00B1, 0x2213, 0x22C5, 0x00D7, 0x00F7, 0x2248, 0x2260, 0x2264, 0x2265,
    # Arrows
    0x2192, 0x2190,
    # Logic/Sets
    0x2208, 0x2282, 0x2234, 0x27E8, 0x27E9,
    # Structural
    0x221A,
]


def shade(value):
    """Four-level coverage with FlowCE's small-text antialiasing curve."""
    alpha = value / 255.0
    if alpha < 0.14:
        return 0

    alpha **= 0.8
    if alpha < 0.42:
        return 1
    if alpha < 0.8:
        return 2
    return 3


def raster(font, text):
    """Return advance, tight ink box, and packed pixels; bearings use baseline."""
    advance = round(font.getlength(text))
    left, top, right, bottom = font.getbbox(text, anchor="ls")
    if right <= left or bottom <= top:
        if text != " ":
            raise ValueError(f"Empty required glyph: {text!r}")
        return advance, 0, 0, 0, 0, b""

    image = Image.new("L", (right - left, bottom - top))
    ImageDraw.Draw(image).text((-left, -top), text, font=font, fill=255, anchor="ls")
    image = image.point([shade(value) for value in range(256)])
    bounds = image.getbbox()
    if bounds is None:
        raise ValueError(f"Required glyph lost during quantization: {text!r}")

    crop_left, crop_top, crop_right, crop_bottom = bounds
    pixels = image.crop(bounds).tobytes()
    packed = bytearray((len(pixels) + 3) // 4)
    for index, value in enumerate(pixels):
        packed[index // 4] |= value << (6 - 2 * (index % 4))

    return (
        advance,
        crop_right - crop_left,
        crop_bottom - crop_top,
        left + crop_left,
        top + crop_top,
        bytes(packed),
    )


def scaled(value, size, em, upward=False):
    """Nearest pixel, ties away from zero; minima and rules round upward."""
    product = value * size
    if upward:
        return -(-product // em)

    if product >= 0:
        sign = 1
    else:
        sign = -1
    return sign * ((abs(product) * 2 + em) // (2 * em))


def build_payload(size):
    with TTFont(FONT_DIR / "lmroman10-regular.otf") as text_metadata, \
            TTFont(FONT_DIR / "latinmodern-math.otf") as math_metadata:
        math_cmap = math_metadata.getBestCmap()
        text_cmap = text_metadata.getBestCmap()
        math_table = math_metadata["MATH"].table
        math_em = math_metadata["head"].unitsPerEm

        metric_values = []
        for metric_name in METRICS:
            if metric_name is None:
                value = size
            else:
                raw_value = getattr(math_table.MathConstants, metric_name)
                raw_value = getattr(raw_value, "Value", raw_value)
                if metric_name.endswith("Percent"):
                    value = raw_value
                else:
                    value = scaled(
                        raw_value,
                        size,
                        math_em,
                        metric_name.endswith(("Min", "Thickness", "VerticalGap")),
                    )
            metric_values.append(value)

        italic_info = math_table.MathGlyphInfo.MathItalicsCorrectionInfo
        italic_corrections = dict(zip(
            italic_info.Coverage.glyphs,
            (correction.Value for correction in italic_info.ItalicsCorrection),
        ))
        accent_info = math_table.MathGlyphInfo.MathTopAccentAttachment
        accent_attachments = dict(zip(
            accent_info.TopAccentCoverage.glyphs,
            (attachment.Value for attachment in accent_info.TopAccentAttachment),
        ))

        # Explicit source selection: never substitute a missing required glyph.
        mapping = {codepoint: (0, text_cmap[codepoint]) for codepoint in range(32, 127)}
        mapping[127] = (0, None)  # The byte API's visible DEL label.
        for codepoint in map(ord, "+=<>|"):
            mapping[codepoint] = (1, math_cmap[codepoint])
        mapping.update(
            (index + 1, (1, math_cmap[codepoint]))
            for index, codepoint in enumerate(LOW_SYMBOLS)
        )

        for index, codepoint in enumerate(SYMBOLS):
            # Mathematical italic Greek follows the Unicode lowercase sequence,
            # including the final-sigma hole; uppercase Greek stays upright.
            if index < 24:
                codepoint = 0x1D6FC + codepoint - 0x03B1
            mapping[128 + index] = (1, math_cmap[codepoint])

        for index in range(52):
            if index < 26:
                codepoint = 0x1D434 + index
            else:
                codepoint = 0x1D44E + index - 26
            if codepoint == 0x1D455:  # Unicode reserves italic h at Planck constant.
                codepoint = 0x210E
            mapping[256 + index] = (1, math_cmap[codepoint])

        mapping[308] = (1, math_cmap[0x2212])
        variants = math_table.MathVariants
        for index, codepoint in enumerate((0x222B, 0x2211, 0x220F)):
            glyph = math_cmap[codepoint]
            construction = variants.VertGlyphConstruction[
                variants.VertGlyphCoverage.glyphs.index(glyph)
            ]
            minimum_height = math_table.MathConstants.DisplayOperatorMinHeight
            candidates = [
                variant
                for variant in construction.MathGlyphVariantRecord
                if variant.AdvanceMeasurement >= minimum_height
            ]
            if not candidates:
                raise ValueError(
                    f"No display variant meets MATH minimum: U+{codepoint:04X}"
                )
            chosen_variant = min(
                candidates, key=lambda variant: variant.AdvanceMeasurement
            )
            mapping[309 + index] = (1, chosen_variant.VariantGlyph)

        # FreeType accepts characters, not glyph IDs. An in-memory cmap exposes
        # the selected CFF glyphs without changing outlines, scale, or bearings.
        fonts = []
        for face_index, metadata in enumerate((text_metadata, math_metadata)):
            remap = {
                0xE000 + slot: glyph
                for slot, (source_index, glyph) in mapping.items()
                if source_index == face_index and glyph is not None
            }
            for table in metadata["cmap"].tables:
                if table.isUnicode():
                    table.cmap.update(remap)

            font_buffer = BytesIO()
            metadata.save(font_buffer)
            font_buffer.seek(0)
            fonts.append(ImageFont.truetype(
                font_buffer, size, layout_engine=ImageFont.Layout.BASIC
            ))

        records = bytearray(GLYPH_COUNT * 9)
        bitmaps = bytearray()
        ascender = scaled(
            text_metadata["OS/2"].sTypoAscender,
            size,
            text_metadata["head"].unitsPerEm,
            upward=True,
        )
        descender = scaled(
            -text_metadata["OS/2"].sTypoDescender,
            size,
            text_metadata["head"].unitsPerEm,
            upward=True,
        )

        for slot, (face_index, glyph) in sorted(mapping.items()):
            if slot == 127:
                text = "DEL"
            elif slot == 32:
                text = " "
            else:
                text = chr(0xE000 + slot)
            advance, width, height, x_bearing, y_bearing, bitmap = raster(
                fonts[face_index], text
            )

            # OpenType MATH defaults: absent italic correction is zero; absent
            # top attachment is half advance. Roman has no MATH table.
            if face_index:
                italic_correction = scaled(
                    italic_corrections.get(glyph, 0), size, math_em
                )
            else:
                italic_correction = 0

            source_metadata = math_metadata if face_index else text_metadata
            source_em = source_metadata["head"].unitsPerEm
            if face_index and glyph in accent_attachments:
                top_attachment = scaled(accent_attachments[glyph], size, math_em)
            elif glyph is not None:
                top_attachment = scaled(
                    source_metadata["hmtx"][glyph][0], size, 2 * source_em
                )
            else:
                top_attachment = (advance + 1) // 2

            if not bitmap:
                italic_correction = 0
                top_attachment = 0

            if (
                not 0 < advance <= 255
                or width > 255
                or height > 255
            ):
                raise ValueError(f"Out-of-range glyph metrics for slot {slot}")
            if not all(
                -128 <= value <= 127
                for value in (x_bearing, y_bearing, italic_correction, top_attachment)
            ):
                raise ValueError(
                    f"Out-of-range glyph attachment/bearing for slot {slot}"
                )

            offset = BITMAP_START + len(bitmaps) if bitmap else 0
            struct.pack_into(
                "<HBBBbbbb",
                records,
                slot * 9,
                offset,
                advance,
                width,
                height,
                x_bearing,
                y_bearing,
                italic_correction,
                top_attachment,
            )
            bitmaps.extend(bitmap)

        if (
            not 0 < ascender <= 255
            or not 0 <= descender <= 255
            or not 0 <= metric_values[1] <= ascender
        ):
            raise ValueError("Out-of-range face metrics")

        payload = (
            b"TXF3"
            + bytes((ascender, descender, len(METRICS), 0))
            + struct.pack(f"<{len(metric_values)}h", *metric_values)
            + records
            + bitmaps
        )
        if len(payload) > 65512:
            raise ValueError("Font exceeds the AppVar payload limit")
        return payload, len(mapping), ascender, descender, metric_values[1]


def appvar(name, payload):
    """Standard TI-83F container, AppVar type 0x15, archived flag 0x80."""
    data = struct.pack("<H", len(payload)) + payload
    entry = struct.pack(
        "<HHB8sBBH", 13, len(data), 0x15,
        name.encode("ascii"), 0, 0x80, len(data),
    ) + data
    comment = b"libtexce TXF3 / Latin Modern / GUST".ljust(42, b"\0")

    return (
        b"**TI83F*\x1a\x0a\x00"
        + comment
        + struct.pack("<H", len(entry))
        + entry
        + struct.pack("<H", sum(entry) & 0xFFFF)
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--outdir",
        type=Path,
        default=ROOT_DIR / "assets",
        help="Output directory (default: repository assets)",
    )
    args = parser.parse_args()
    args.outdir.mkdir(parents=True, exist_ok=True)

    with TTFont(FONT_DIR / "latinmodern-math.otf") as math_metadata:
        script_percent = math_metadata["MATH"].table.MathConstants.ScriptPercentScaleDown
    # Explicit half-up rounding: Latin Modern's 70% turns 16px into 11px.
    script_size = (16 * script_percent + 50) // 100

    for name, size in (("TeXFonts", 16), ("TeXScrpt", script_size)):
        payload, glyph_count, ascender, descender, axis_height = build_payload(size)
        container = appvar(name, payload)
        output_path = args.outdir / f"{name}.8xv"
        output_path.write_bytes(container)
        print(
            f"{output_path}: {size}px, {glyph_count} glyphs, "
            f"asc={ascender} desc={descender} axis={axis_height}, "
            f"{len(payload)} payload bytes / {len(container)} file bytes, archived"
        )


if __name__ == "__main__":
    main()
