#ifndef TEX_TEX_FONTS_H
#define TEX_TEX_FONTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Borrowed TXF3 payload */
typedef struct
{
	const uint8_t* data;
} TeX_Font;

typedef struct
{
	const uint8_t* pixels;
	uint8_t advance;
	uint8_t width;
	uint8_t height;

	int8_t x_offset;
	int8_t y_offset; /* top of bitmap relative to baseline, negative is above */
	int8_t italic_correction;
	int8_t top_accent; /* attachment x coordinate relative to glyph origin */
} TexGlyph;

typedef struct
{
	TeX_Font main_font;
	TeX_Font script_font;
} TexFontHandles;

typedef enum
{
	TEX_FONT_EM,
	TEX_FONT_AXIS,

	TEX_FONT_SUP_SHIFT,
	TEX_FONT_SUP_CRAMPED_SHIFT,
	TEX_FONT_SUP_BOTTOM_MIN,
	TEX_FONT_SUP_DROP_MAX,
	TEX_FONT_SUB_SHIFT,
	TEX_FONT_SUB_TOP_MAX,
	TEX_FONT_SUB_DROP_MIN,
	TEX_FONT_SUB_SUP_GAP,
	TEX_FONT_SUP_BOTTOM_WITH_SUB,
	TEX_FONT_SPACE_AFTER_SCRIPT,

	TEX_FONT_UPPER_LIMIT_GAP,
	TEX_FONT_UPPER_LIMIT_RISE,
	TEX_FONT_LOWER_LIMIT_GAP,
	TEX_FONT_LOWER_LIMIT_DROP,

	TEX_FONT_FRAC_NUM_SHIFT,
	TEX_FONT_FRAC_DEN_SHIFT,
	TEX_FONT_FRAC_NUM_DISPLAY_SHIFT,
	TEX_FONT_FRAC_DEN_DISPLAY_SHIFT,
	TEX_FONT_FRAC_NUM_GAP,
	TEX_FONT_FRAC_DEN_GAP,
	TEX_FONT_FRAC_NUM_DISPLAY_GAP,
	TEX_FONT_FRAC_DEN_DISPLAY_GAP,
	TEX_FONT_FRAC_RULE,

	TEX_FONT_RADICAL_GAP,
	TEX_FONT_RADICAL_DISPLAY_GAP,
	TEX_FONT_RADICAL_RULE,
	TEX_FONT_RADICAL_EXTRA_ASC,
	TEX_FONT_RADICAL_KERN_BEFORE,
	TEX_FONT_RADICAL_KERN_AFTER,
	TEX_FONT_RADICAL_DEGREE_RAISE,

	TEX_FONT_METRIC_COUNT
} TexFontMetric;

bool tex_font_init(TeX_Font* out, const void* data, size_t size);
bool tex_font_open(TeX_Font* out, const char* appvar);

int16_t tex_font_asc(TeX_Font font);
int16_t tex_font_desc(TeX_Font font);
int16_t tex_font_metric(TeX_Font font, TexFontMetric metric);

uint16_t tex_font_math_code(uint8_t code, bool display);

bool tex_font_glyph(TeX_Font font, uint16_t code, TexGlyph* out);

#ifdef __cplusplus
}
#endif

#endif /* TEX_TEX_FONTS_H */
