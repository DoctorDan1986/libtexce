#include "tex_fonts.h"

#include <fileioc.h>

/* TXF3: magic, normal asc/desc, metric count, zero; signed LE16 metrics;
 * 312 nine-byte records: LE16 offset, advance, width, height, x/y, italic,
 * top-accent. Contiguous row-major 2-bit pixels, high bits first, no row padding.
 * Display operators may exceed the normal line bounds. No runtime font cache.
 */
#define TEX_FONT_HEADER_SIZE (8u + 2u * TEX_FONT_METRIC_COUNT)
#define TEX_FONT_RECORD_SIZE 9u
#define TEX_FONT_GLYPH_COUNT 312u
#define TEX_FONT_BITMAP_START (TEX_FONT_HEADER_SIZE + TEX_FONT_GLYPH_COUNT * TEX_FONT_RECORD_SIZE)

enum
{
	TEX_FONT_RECORD_ADVANCE = 2,
	TEX_FONT_RECORD_WIDTH = 3,
	TEX_FONT_RECORD_HEIGHT = 4,
	TEX_FONT_RECORD_X_OFFSET = 5,
	TEX_FONT_RECORD_Y_OFFSET = 6,
	TEX_FONT_RECORD_ITALIC_CORRECTION = 7,
	TEX_FONT_RECORD_TOP_ACCENT = 8
};

static uint16_t tex_font_u16(const uint8_t* bytes)
{
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

int16_t tex_font_metric(TeX_Font font, TexFontMetric metric)
{
	return (int16_t)tex_font_u16(font.data + 8u + 2u * (unsigned int)metric);
}

bool tex_font_init(TeX_Font* out, const void* data, size_t size)
{
	if (!out)
		return false;

	out->data = NULL;

	// Validate the fixed header before reading metrics or glyph records.
	const uint8_t* bytes = data;
	if (!bytes || size < TEX_FONT_BITMAP_START || size > 65512u)
		return false;

	if (bytes[0] != 'T' || bytes[1] != 'X' || bytes[2] != 'F' || bytes[3] != '3')
		return false;

	if (!bytes[4] || bytes[6] != TEX_FONT_METRIC_COUNT || bytes[7])
		return false;

	TeX_Font font_view = { bytes };
	int16_t em_size = tex_font_metric(font_view, TEX_FONT_EM);
	if (em_size <= 0)
		return false;

	// Radical kerns may be negative; the degree raise is a percentage.
	for (unsigned int metric = 1; metric < TEX_FONT_METRIC_COUNT; ++metric)
	{
		int16_t value = tex_font_metric(font_view, (TexFontMetric)metric);
		if (metric == TEX_FONT_RADICAL_DEGREE_RAISE)
		{
			if (value < 0 || value > 100)
				return false;
		}
		else if (value < 0 &&
		         metric != TEX_FONT_RADICAL_KERN_BEFORE &&
		         metric != TEX_FONT_RADICAL_KERN_AFTER)
		{
			return false;
		}
	}

	if (tex_font_metric(font_view, TEX_FONT_AXIS) > bytes[4] ||
	    !tex_font_metric(font_view, TEX_FONT_FRAC_RULE) ||
	    !tex_font_metric(font_view, TEX_FONT_RADICAL_RULE))
	{
		return false;
	}

	// Nonempty glyphs must occupy the bitmap area consecutively.
	size_t bitmap_cursor = TEX_FONT_BITMAP_START;
	for (unsigned int code = 0; code < TEX_FONT_GLYPH_COUNT; ++code)
	{
		const uint8_t* glyph_record = bytes + TEX_FONT_HEADER_SIZE + code * TEX_FONT_RECORD_SIZE;
		size_t offset = tex_font_u16(glyph_record);
		uint8_t width = glyph_record[TEX_FONT_RECORD_WIDTH];
		uint8_t height = glyph_record[TEX_FONT_RECORD_HEIGHT];

		if (!width || !height)
		{
			// Only space has an advance without ink; missing entries are all zero.
			if (offset || width || height ||
			    glyph_record[TEX_FONT_RECORD_X_OFFSET] ||
			    glyph_record[TEX_FONT_RECORD_Y_OFFSET] ||
			    glyph_record[TEX_FONT_RECORD_ITALIC_CORRECTION] ||
			    glyph_record[TEX_FONT_RECORD_TOP_ACCENT] ||
			    (glyph_record[TEX_FONT_RECORD_ADVANCE] && code != ' '))
			{
				return false;
			}

			continue;
		}

		if (!glyph_record[TEX_FONT_RECORD_ADVANCE])
			return false;

		size_t pixel_count = (size_t)width * height;
		size_t bitmap_size = (pixel_count + 3u) / 4u;
		if (offset != bitmap_cursor || bitmap_size > size - bitmap_cursor)
			return false;

		// Unused low bits of each glyph's last byte must be zero.
		unsigned int remainder = pixel_count & 3u;
		if (remainder)
		{
			unsigned int padding_mask = (1u << (8u - 2u * remainder)) - 1u;
			if (bytes[offset + bitmap_size - 1u] & padding_mask)
				return false;
		}

		bitmap_cursor += bitmap_size;
	}

	if (bitmap_cursor != size)
		return false;

	out->data = bytes;
	return true;
}

bool tex_font_open(TeX_Font* out, const char* appvar)
{
	if (!out)
		return false;

	out->data = NULL;
	if (!appvar)
		return false;

	ti_var_t handle = ti_Open(appvar, "r");
	if (!handle)
		return false;

	bool valid = tex_font_init(out, ti_GetDataPtr(handle), ti_GetSize(handle));
	ti_Close(handle);

	return valid;
}

int16_t tex_font_asc(TeX_Font font)
{
	return font.data[4];
}

int16_t tex_font_desc(TeX_Font font)
{
	return font.data[5];
}

uint16_t tex_font_math_code(uint8_t code, bool display)
{
	if (code >= 'A' && code <= 'Z')
		return 256u + code - 'A';

	if (code >= 'a' && code <= 'z')
		return 282u + code - 'a';

	if (code == '-')
		return 308u;

	if (display && code >= 0xa9u && code <= 0xabu)
		return 309u + code - 0xa9u;

	return code;
}

bool tex_font_glyph(TeX_Font font, uint16_t code, TexGlyph* out)
{
	*out = (TexGlyph){ 0 };

	const uint8_t* glyph_record = font.data + TEX_FONT_HEADER_SIZE + code * TEX_FONT_RECORD_SIZE;

	if (!glyph_record[TEX_FONT_RECORD_ADVANCE])
		return false;

	out->advance = glyph_record[TEX_FONT_RECORD_ADVANCE];
	out->width = glyph_record[TEX_FONT_RECORD_WIDTH];
	out->height = glyph_record[TEX_FONT_RECORD_HEIGHT];
	out->x_offset = (int8_t)glyph_record[TEX_FONT_RECORD_X_OFFSET];
	out->y_offset = (int8_t)glyph_record[TEX_FONT_RECORD_Y_OFFSET];
	out->italic_correction = (int8_t)glyph_record[TEX_FONT_RECORD_ITALIC_CORRECTION];
	out->top_accent = (int8_t)glyph_record[TEX_FONT_RECORD_TOP_ACCENT];
	out->pixels = out->width ? font.data + tex_font_u16(glyph_record) : NULL;

	return true;
}
