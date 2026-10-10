#include "tex_metrics.h"
#include "tex_internal.h"
#include "tex_util.h"
#include <limits.h>

static TexFontHandles g_fonts;

static void tex_reserved_init(void);

TeX_Font tex_metrics_font(FontRole role)
{
	return role == FONTROLE_SCRIPT ? g_fonts.script_font : g_fonts.main_font;
}

void tex_metrics_activate(const TexFontHandles* fonts)
{
	if (!fonts)
	{
		g_fonts = (TexFontHandles){ 0 };
		return;
	}

	int changed = g_fonts.main_font.data != fonts->main_font.data ||
	    g_fonts.script_font.data != fonts->script_font.data;
	g_fonts = *fonts;

	if (changed)
		tex_reserved_init();
}

void tex_metrics_init(struct TeX_Layout* layout)
{
	TeX_Font main = layout->cfg.font_main;
	TeX_Font script = layout->cfg.font_script;
	if (!main.data || !script.data)
	{
		tex_metrics_activate(NULL);
		TEX_SET_ERROR(layout, TEX_ERR_FONT, "Invalid font handles", 0);
		return;
	}

	layout->fonts.main_font = main;
	layout->fonts.script_font = script;
	tex_metrics_activate(&layout->fonts);
}


int16_t tex_metrics_asc(FontRole role)
{
	return tex_font_asc(tex_metrics_font(role));
}

int16_t tex_metrics_desc(FontRole role)
{
	return tex_font_desc(tex_metrics_font(role));
}

TexTextMetrics tex_metrics_text(const char* s, int len, FontRole role)
{
	TexTextMetrics metrics = { 0 };
	TeX_Font font = tex_metrics_font(role);

	for (int i = 0; i < len; ++i)
	{
		TexGlyph glyph;
		if (tex_font_glyph(font, (uint8_t)s[i], &glyph))
		{
			metrics.w = (int16_t)TEX_MIN(INT16_MAX, metrics.w + glyph.advance);
			metrics.asc = (int16_t)TEX_MAX(metrics.asc, -glyph.y_offset);
			metrics.desc = (int16_t)TEX_MAX(metrics.desc, glyph.y_offset + glyph.height);
		}
	}

	return metrics;
}

bool tex_metrics_glyph(const Node* node, TexGlyph* glyph)
{
	FontRole role = (node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN;
	uint8_t code = node->type == N_MULTIOP ? (uint8_t)TEXFONT_INTEGRAL_CHAR : (uint8_t)node->data.glyph;

	return tex_font_glyph(tex_metrics_font(role),
	                      tex_font_math_code(code, (node->flags & TEX_FLAG_MATHF_DISPLAY) != 0), glyph);
}

void tex_metrics_measure_glyph(Node* node)
{
	TexGlyph glyph;
	node->w = 0;
	node->asc = 0;
	node->desc = 0;

	if (tex_metrics_glyph(node, &glyph))
	{
		node->w = (int16_t)(glyph.advance + glyph.italic_correction);
		node->asc = (int16_t)TEX_MAX(0, -glyph.y_offset);
		node->desc = (int16_t)TEX_MAX(0, glyph.y_offset + glyph.height);
	}
}


Node g_reserved_nodes[TEX_RESERVED_COUNT];

static void tex_reserved_init(void)
{
	// main role glyphs (0-127)
	for (int i = 0; i < 128; i++)
	{
		Node* node = &g_reserved_nodes[i];
		node->type = N_GLYPH;
		node->flags = 0;
		node->data.glyph = (uint16_t)i;

		tex_metrics_measure_glyph(node);
	}

	// script role glyphs (128-255)
	for (int i = 128; i < TEX_RESERVED_COUNT; i++)
	{
		Node* node = &g_reserved_nodes[i];
		node->type = N_GLYPH;
		node->flags = TEX_FLAG_SCRIPT;

		// back to ASCII (e.g., 128 -> 0) for the actual glyph code
		uint16_t ascii_code = (uint16_t)(i - 128);
		node->data.glyph = ascii_code;

		tex_metrics_measure_glyph(node);
	}
}

