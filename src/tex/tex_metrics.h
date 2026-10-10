#ifndef TEX_TEX_METRICS_H
#define TEX_TEX_METRICS_H

#include "tex.h"
#include "tex_measure.h"
#include "texfont.h"
#include "tex_fonts.h"

static inline int tex_is_big_operator(unsigned int glyph)
{
	unsigned char g = (unsigned char)glyph;
	return g == (unsigned char)TEXFONT_INTEGRAL_CHAR || g == (unsigned char)TEXFONT_SUMMATION_CHAR ||
	    g == (unsigned char)TEXFONT_PRODUCT_CHAR;
}

// forward declaration
struct TeX_Layout;

void tex_metrics_init(struct TeX_Layout* layout);

void tex_metrics_activate(const TexFontHandles* fonts);
TeX_Font tex_metrics_font(FontRole role);

// query per role metrics
int16_t tex_metrics_asc(FontRole role);
int16_t tex_metrics_desc(FontRole role);

typedef struct
{
	int16_t w;
	int16_t asc;
	int16_t desc;
} TexTextMetrics;

// advance w and glyph ink bounds
// line spacing is applied by the caller
TexTextMetrics tex_metrics_text(const char* s, int len, FontRole role);
bool tex_metrics_glyph(const Node* node, TexGlyph* glyph);
void tex_metrics_measure_glyph(Node* node);


#endif // TEX_TEX_METRICS_H
