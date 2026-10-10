#include "test_check.h"
#include <stdio.h>

#include "tex/tex_fonts.h"

int main(void)
{
	enum
	{
		HEADER = 8 + 2 * TEX_FONT_METRIC_COUNT,
		BITMAP = HEADER + 312 * 9,
		LETTER = HEADER + 'A' * 9,
		SPACE = HEADER + ' ' * 9,
		ITALIC = HEADER + 256 * 9,
		DISPLAY = HEADER + 309 * 9
	};
	uint8_t blob[BITMAP + 4] = {
		'T', 'X', 'F', '3', 3, 2, TEX_FONT_METRIC_COUNT, 0,
		// Upright A: offset, advance, ink dimensions, bearings, accent anchor.
		[LETTER] = BITMAP & 255,
		[LETTER + 1] = BITMAP >> 8,
		[LETTER + 2] = 4,
		[LETTER + 3] = 3,
		[LETTER + 4] = 2,
		[LETTER + 5] = 255,
		[LETTER + 6] = 254,
		[LETTER + 8] = 2,

		[SPACE + 2] = 2,

		// Italic A includes italic correction and a top-accent anchor.
		[ITALIC] = (BITMAP + 2) & 255,
		[ITALIC + 1] = (BITMAP + 2) >> 8,
		[ITALIC + 2] = 4,
		[ITALIC + 3] = 1,
		[ITALIC + 4] = 1,
		[ITALIC + 6] = 253,
		[ITALIC + 7] = 1,
		[ITALIC + 8] = 3,

		// Display ink can exceed line bounds and any em multiple.
		[DISPLAY] = (BITMAP + 3) & 255,
		[DISPLAY + 1] = (BITMAP + 3) >> 8,
		[DISPLAY + 2] = 5,
		[DISPLAY + 3] = 1,
		[DISPLAY + 4] = 1,
		[DISPLAY + 6] = 156,

		[BITMAP] = 0x1b,
		[BITMAP + 1] = 0xe0,
		[BITMAP + 2] = 0xc0,
		[BITMAP + 3] = 0x80
	};

	for (unsigned int metric = 0; metric < TEX_FONT_METRIC_COUNT; ++metric)
	{
		blob[8 + metric * 2] = 1;
	}
	blob[8 + TEX_FONT_EM * 2] = 4;
	blob[8 + TEX_FONT_RADICAL_KERN_AFTER * 2] = 254;
	blob[9 + TEX_FONT_RADICAL_KERN_AFTER * 2] = 255;
	blob[8 + TEX_FONT_RADICAL_DEGREE_RAISE * 2] = 60;

	TeX_Font font = { 0 };
	CHECK(tex_font_init(&font, blob, sizeof(blob)));
	CHECK(font.data == blob);
	CHECK(tex_font_asc(font) == 3 && tex_font_desc(font) == 2);
	CHECK(tex_font_metric(font, TEX_FONT_EM) == 4);
	CHECK(tex_font_metric(font, TEX_FONT_AXIS) == 1);
	CHECK(tex_font_metric(font, TEX_FONT_RADICAL_KERN_AFTER) == -2);
	CHECK(tex_font_metric(font, TEX_FONT_RADICAL_DEGREE_RAISE) == 60);

	TexGlyph glyph;
	CHECK(tex_font_glyph(font, 'A', &glyph));
	CHECK(glyph.pixels == blob + BITMAP);
	CHECK(glyph.advance == 4 && glyph.width == 3 && glyph.height == 2);
	CHECK(glyph.x_offset == -1 && glyph.y_offset == -2);
	CHECK(glyph.italic_correction == 0 && glyph.top_accent == 2);

	const uint8_t coverage[] = { 0, 1, 2, 3, 3, 2 };
	for (size_t pixel = 0; pixel < sizeof(coverage); ++pixel)
	{
		CHECK(((glyph.pixels[pixel / 4] >> (6 - 2 * (pixel % 4))) & 3) == coverage[pixel]);
	}

	CHECK(tex_font_glyph(font, tex_font_math_code('A', false), &glyph));
	CHECK(glyph.pixels == blob + BITMAP + 2);
	CHECK(glyph.italic_correction == 1 && glyph.top_accent == 3);

	CHECK(tex_font_glyph(font, tex_font_math_code(0xa9, true), &glyph));
	CHECK(glyph.pixels == blob + BITMAP + 3 && glyph.y_offset == -100);

	CHECK(tex_font_glyph(font, ' ', &glyph));
	CHECK(glyph.advance == 2 && !glyph.pixels && !glyph.width && !glyph.height);
	CHECK(!glyph.x_offset && !glyph.y_offset && !glyph.italic_correction && !glyph.top_accent);

	CHECK(!tex_font_glyph(font, 0, &glyph));
	CHECK(!tex_font_glyph(font, 255, &glyph));
	CHECK(!glyph.pixels && !glyph.advance);

	for (size_t size = 0; size < sizeof(blob); ++size)
	{
		font.data = blob;
		CHECK(!tex_font_init(&font, blob, size));
		CHECK(!font.data);
	}

	CHECK(!tex_font_init(NULL, blob, sizeof(blob)));
	CHECK(!tex_font_init(&font, blob, 65513u) && !font.data);
	font.data = blob;
	CHECK(!tex_font_init(&font, NULL, sizeof(blob)) && !font.data);

	const struct
	{
		size_t offset;
		uint8_t value;
	} invalid[] = {
		{ 0, 'Y' },
		{ 3, '2' },
		{ 4, 0 },
		{ 6, TEX_FONT_METRIC_COUNT - 1 },
		{ 7, 1 },
		{ 8 + TEX_FONT_EM * 2, 0 },
		{ 9 + TEX_FONT_AXIS * 2, 255 }, /* Negative axis. */
		{ 8 + TEX_FONT_AXIS * 2, 4 },
		{ 8 + TEX_FONT_RADICAL_DEGREE_RAISE * 2, 101 },
		{ 8 + TEX_FONT_FRAC_RULE * 2, 0 },
		{ 8 + TEX_FONT_RADICAL_RULE * 2, 0 },
		{ LETTER, 0 },              /* Bitmap inside the fixed table. */
		{ LETTER + 1, 255 },        /* Offset beyond payload. */
		{ LETTER + 2, 0 },          /* Nonempty glyph needs an advance. */
		{ LETTER + 3, 255 },        /* Bitmap extends beyond payload. */
		{ LETTER + 4, 0 },          /* Partial empty dimensions. */
		{ ITALIC, BITMAP & 255 },   /* Overlapping bitmaps. */
		{ SPACE + 5, 1 },
		{ SPACE + 7, 1 },
		{ SPACE + 8, 1 },
		{ HEADER + 255 * 9 + 2, 1 }, /* Only space can have advance without ink. */
		{ BITMAP + 1, 0xe1 },       /* Nonzero tail padding. */
		{ BITMAP + 3, 0x81 }        /* Nonzero tail padding. */
	};

	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
	{
		uint8_t saved = blob[invalid[i].offset];
		blob[invalid[i].offset] = invalid[i].value;
		font.data = blob;
		CHECK(!tex_font_init(&font, blob, sizeof(blob)) && !font.data);
		blob[invalid[i].offset] = saved;
	}

	/* Extra bytes are not silently accepted as a second payload. */
	uint8_t extra[sizeof(blob) + 1];
	for (size_t byte = 0; byte < sizeof(blob); ++byte)
	{
		extra[byte] = blob[byte];
	}
	extra[sizeof(blob)] = 0;
	CHECK(!tex_font_init(&font, extra, sizeof(extra)) && !font.data);
	CHECK(tex_font_init(&font, blob, sizeof(blob)) && font.data == blob);

	puts("test_fonts: PASS (TXF3 borrowed glyphs, MATH metadata, malformed payloads)");
	return 0;
}
