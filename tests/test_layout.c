#include "test_check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <graphx.h>
#include "tex/tex.h"
#include "tex/tex_renderer.h"
#include "tex/tex_measure.h"
#include <PortCE.h>

static uint32_t draw_hash(TeX_Renderer* renderer, TeX_Layout* layout, int scroll)
{
	gfx_FillScreen(255);
	tex_draw(renderer, layout, 0, 0, scroll);
	CHECK(tex_get_last_error(layout) == TEX_OK);

	uint32_t hash = UINT32_C(2166136261);
	for (size_t i = 0; i < (size_t)GFX_LCD_WIDTH * GFX_LCD_HEIGHT; ++i)
	{
		hash = (hash ^ gfx_vram[i]) * UINT32_C(16777619);
	}
	return hash;
}

static void check_replay(const char* source, int width, const TeX_Config* config)
{
	TeX_Layout* layout = tex_format(source, width, config);
	TeX_Renderer* renderer = tex_renderer_create();
	CHECK(layout && renderer && tex_get_last_error(layout) == TEX_OK);
	CHECK(layout->checkpoint_count > 0);

	int scroll = layout->checkpoints[0].y_pos + TEX_RENDERER_PADDING;
	uint32_t indexed = draw_hash(renderer, layout, scroll);
	CHECK(renderer->window_y_end >= scroll + GFX_LCD_HEIGHT);

	// The reference starts at the document beginning, bypassing the sparse index.
	int count = layout->checkpoint_count;
	layout->checkpoint_count = 0;
	tex_renderer_invalidate(renderer);
	CHECK(draw_hash(renderer, layout, scroll) == indexed);
	CHECK(renderer->window_y_end >= scroll + GFX_LCD_HEIGHT);
	layout->checkpoint_count = count;

	// A complete cached window must not parse or allocate on an identical draw.
	size_t resets = renderer->pool.reset_count;
	size_t allocations = renderer->pool.alloc_count;
	CHECK(draw_hash(renderer, layout, scroll) == indexed);
	CHECK(renderer->pool.reset_count == resets && renderer->pool.alloc_count == allocations);

	int bottom = tex_get_total_height(layout) - GFX_LCD_HEIGHT;
	uint32_t last_page = draw_hash(renderer, layout, bottom);
	resets = renderer->pool.reset_count;
	allocations = renderer->pool.alloc_count;
	CHECK(draw_hash(renderer, layout, bottom) == last_page);
	CHECK(renderer->pool.reset_count == resets && renderer->pool.alloc_count == allocations);

	tex_free(layout);
	tex_renderer_destroy(renderer);
}

static void check_cache_and_fonts(const TeX_Config* config)
{
	TeX_Renderer* renderer = tex_renderer_create();
	const char* source = "Hello $x^2$ \\%";
	TeX_Layout* first = tex_format(source, 300, config);
	CHECK(renderer && first);
	CHECK(tex_get_total_height(first) < GFX_LCD_HEIGHT);

	uint32_t initial = draw_hash(renderer, first, 0);
	size_t resets = renderer->pool.reset_count;
	size_t allocations = renderer->pool.alloc_count;
	CHECK(draw_hash(renderer, first, 0) == initial);
	CHECK(renderer->pool.reset_count == resets && renderer->pool.alloc_count == allocations);

	TeX_Config other = *config;
	other.font_main = config->font_script;
	other.font_script = config->font_main;
	TeX_Layout* second = tex_format(source, 300, &other);
	CHECK(second);
	CHECK(draw_hash(renderer, first, 0) == initial); // formatting selected different fonts
	CHECK(draw_hash(renderer, second, 0) != initial);
	CHECK(draw_hash(renderer, first, 0) == initial);
	tex_free(first);

	// Reusing a renderer after its cached layout is freed must not retain that
	// layout's content, including when the allocator reuses its address.
	first = tex_format("Different content $\\frac{a}{b}$", 300, config);
	CHECK(first);
	uint32_t replacement = draw_hash(renderer, first, 0);
	CHECK(replacement != initial);
	tex_renderer_invalidate(renderer);
	CHECK(draw_hash(renderer, first, 0) == replacement);
	tex_renderer_destroy(renderer);
	tex_free(first);
	tex_free(second);

	renderer = tex_renderer_create();
	first = tex_format("", 300, config);
	CHECK(renderer && first && tex_get_total_height(first) == 0);
	uint32_t empty = draw_hash(renderer, first, 0);
	resets = renderer->pool.reset_count;
	CHECK(draw_hash(renderer, first, 0) == empty);
	CHECK(renderer->pool.reset_count == resets);
	tex_free(first);
	tex_renderer_destroy(renderer);
}

static void check_antialias_and_clipping(const TeX_Config* config)
{
	TeX_Layout* layout = tex_format("Ag $\\frac{x+1}{y^2}+\\left(\\sqrt{x}\\right)$", 300, config);
	TeX_Renderer* renderer = tex_renderer_create();
	CHECK(layout && renderer && tex_get_last_error(layout) == TEX_OK);

	uint16_t original_palette[256];
	gfx_palette[config->palette[0]] = 0x7FFF;
	gfx_palette[config->palette[3]] = 0;
	memcpy(original_palette, gfx_palette, sizeof(original_palette));
	uint8_t old_color = gfx_SetColor(17); // strokes must use the configured ink
	(void)draw_hash(renderer, layout, 0);
	static uint8_t reference[GFX_LCD_WIDTH * GFX_LCD_HEIGHT];
	memcpy(reference, gfx_vram, sizeof(reference));

	unsigned int levels = 0;
	for (size_t i = 0; i < sizeof(reference); ++i)
	{
		int found = 0;
		for (unsigned int level = 0; level < 4; ++level)
		{
			if (reference[i] == config->palette[level])
			{
				levels |= 1u << level;
				found = 1;
			}
		}
		CHECK(found);
	}
	CHECK(levels == 15); // background, both coverage shades, and full ink
	CHECK(gfx_palette[config->palette[1]] == 0x5294);
	CHECK(gfx_palette[config->palette[2]] == 0x294A);
	for (unsigned int i = 0; i < 256; ++i)
	{
		if (i != config->palette[1] && i != config->palette[2])
		{
			CHECK(gfx_palette[i] == original_palette[i]);
		}
	}

	gfx_SetClipRegion(7, 4, 91, 37);
	gfx_FillScreen(113);
	for (int row = 4; row < 37; ++row)
	{
		memset(gfx_vram + (ptrdiff_t)row * GFX_LCD_WIDTH + 7, config->palette[0], 91 - 7);
	}
	tex_draw(renderer, layout, 0, 0, 0);
	for (int row = 0; row < GFX_LCD_HEIGHT; ++row)
	{
		for (int col = 0; col < GFX_LCD_WIDTH; ++col)
		{
			size_t pos = (size_t)row * GFX_LCD_WIDTH + (size_t)col;
			CHECK(gfx_vram[pos] == (row >= 4 && row < 37 && col >= 7 && col < 91 ? reference[pos] : 113));
		}
	}

	gfx_SetClipRegion(0, 0, GFX_LCD_WIDTH, GFX_LCD_HEIGHT);
	gfx_FillScreen(config->palette[0]);
	tex_draw(renderer, layout, -3, 0, 3);
	for (int row = 0; row < GFX_LCD_HEIGHT - 3; ++row)
	{
		for (int col = 0; col < GFX_LCD_WIDTH - 3; ++col)
		{
			CHECK(gfx_vram[row * GFX_LCD_WIDTH + col] == reference[(row + 3) * GFX_LCD_WIDTH + col + 3]);
		}
	}

	memcpy(gfx_vram, reference, sizeof(reference));
	gfx_SetDrawBuffer();
	gfx_FillScreen(config->palette[0]);
	tex_draw(renderer, layout, 0, 0, 0);
	CHECK(memcmp(&gfx_vbuffer[0][0], reference, sizeof(reference)) == 0);
	CHECK(memcmp(gfx_vram, reference, sizeof(reference)) == 0);

	gfx_SetDrawScreen();
	(void)gfx_SetColor(old_color);
	tex_renderer_destroy(renderer);
	tex_free(layout);
}

static void check_accent_ink(const TeX_Config* config)
{
	TeX_Layout* layout = tex_format("$\\tilde{x}$", 300, config);
	TeX_Renderer* renderer = tex_renderer_create();
	CHECK(layout && renderer);
	gfx_FillScreen(config->palette[0]);
	tex_draw(renderer, layout, 10, 10, 0);

	TexGlyph accent;
	TexGlyph base;
	CHECK(tex_font_glyph(config->font_main, '~', &accent));
	CHECK(tex_font_glyph(config->font_main, tex_font_math_code('x', false), &base));
	int left = 10 + base.top_accent - accent.width / 2;
	int top = 10 + renderer->lines[0].asc + base.y_offset - TEX_ACCENT_GAP - accent.height;
	for (unsigned int row = 0; row < accent.height; ++row)
	{
		for (unsigned int col = 0; col < accent.width; ++col)
		{
			unsigned int index = row * accent.width + col;
			unsigned int level = (accent.pixels[index / 4] >> (6u - 2u * (index % 4))) & 3u;
			CHECK(gfx_vram[(top + (int)row) * GFX_LCD_WIDTH + left + (int)col] == config->palette[level]);
		}
	}

	tex_renderer_destroy(renderer);
	tex_free(layout);
}

static void check_script_delimiter_clearance(const TeX_Config* config)
{
	TeX_Layout* layout = tex_format("$e^{\\frac{1}{2}\\left(\\frac{x-\\mu}{\\sigma}\\right)}$", 300, config);
	TeX_Renderer* renderer = tex_renderer_create();
	CHECK(layout && renderer);
	gfx_FillScreen(config->palette[0]);
	tex_draw(renderer, layout, 10, 10, 0);
	CHECK(tex_get_last_error(layout) == TEX_OK);

	UnifiedPool* pool = &renderer->pool;
	Node* script = NULL;
	for (size_t ref = 0; ref < pool->node_count; ++ref)
	{
		Node* node = pool_get_node(pool, (NodeRef)ref);
		if (node->type == N_SCRIPT)
		{
			script = node;
		}
	}
	CHECK(script);

	TexScriptGeometry placement;
	tex_measure_script_geometry(pool, script, &placement);
	Node* sup = pool_get_node(pool, script->data.script.sup);
	TexMathCursor cursor;
	tex_math_begin(&cursor, sup->data.list.head, FONTROLE_SCRIPT);
	int offset = 0;
	Node* delim;
	while ((delim = tex_math_next(pool, &cursor, &offset)) && delim->type != N_AUTO_DELIM)
	{
	}
	CHECK(delim);
	int x = 10 + placement.sup_x + offset;
	int baseline = 10 + renderer->lines[0].asc + placement.sup_y;
	TexDelimiterGeometry left;
	TexDelimiterGeometry right;
	tex_measure_delimiter_geometry(DELIM_PAREN, true, delim->data.auto_delim.delim_h, FONTROLE_SCRIPT, &left);
	tex_measure_delimiter_geometry(DELIM_PAREN, false, delim->data.auto_delim.delim_h, FONTROLE_SCRIPT, &right);

	// The font's blank left bearing must separate each curve from the preceding fraction.
	TexGlyph paren;
	CHECK(tex_font_glyph(config->font_script, '(', &paren));
	for (int row = baseline - delim->asc; row < baseline + delim->desc; ++row)
	{
		for (int col = 0; col < paren.x_offset; ++col)
		{
			CHECK(gfx_vram[row * GFX_LCD_WIDTH + x + col] == config->palette[0]);
			CHECK(gfx_vram[row * GFX_LCD_WIDTH + x + delim->w - right.advance + col] == config->palette[0]);
		}
	}

	// The numerator must appear intact after the left delimiter, not shifted into its curve.
	tex_math_begin(&cursor, delim->data.auto_delim.content, FONTROLE_SCRIPT);
	Node* fraction = tex_math_next(pool, &cursor, &offset);
	CHECK(fraction && fraction->type == N_FRAC);
	TexFracGeometry frac;
	tex_measure_frac_geometry(pool, fraction, &frac);
	TexGlyph numerator;
	CHECK(tex_font_glyph(config->font_script, tex_font_math_code('x', false), &numerator));
	int gx = x + left.advance + frac.num_x + numerator.x_offset;
	int gy = baseline + frac.num_y + numerator.y_offset;
	for (unsigned int row = 0; row < numerator.height; ++row)
	{
		for (unsigned int col = 0; col < numerator.width; ++col)
		{
			unsigned int index = row * numerator.width + col;
			unsigned int level = (numerator.pixels[index / 4] >> (6u - 2u * (index % 4))) & 3u;
			CHECK(gfx_vram[(gy + (int)row) * GFX_LCD_WIDTH + gx + (int)col] == config->palette[level]);
		}
	}

	// Inclusive stroke endpoints must still fit inside the half-open measured box.
	for (int col = x; col < x + delim->w; ++col)
	{
		CHECK(gfx_vram[(baseline + delim->desc) * GFX_LCD_WIDTH + col] == config->palette[0]);
	}

	tex_renderer_destroy(renderer);
	tex_free(layout);
}

static void check_text_ink_bounds(const TeX_Config* config)
{
	enum
	{
		HEADER = 8 + 2 * TEX_FONT_METRIC_COUNT,
		GLYPHS = 312,
		RECORD = 9
	};
	size_t size = HEADER + GLYPHS * RECORD;
	for (int code = 0; code < GLYPHS; ++code)
	{
		TexGlyph glyph;
		if (tex_font_glyph(config->font_main, (uint16_t)code, &glyph))
		{
			size += ((size_t)glyph.width * glyph.height + 3) / 4;
		}
	}

	uint8_t* payload = malloc(size);
	CHECK(payload);
	memcpy(payload, config->font_main.data, size);

	// Modify a stationary private copy before any layout borrows it.
	payload[HEADER + 'A' * RECORD + 6] = (uint8_t)-100;
	payload[HEADER + 'B' * RECORD + 6] = 40;
	TeX_Config custom = *config;
	bool accepted = tex_font_init(&custom.font_main, payload, size);
	CHECK(accepted);

	TexGlyph glyphs[2];
	bool have_a = tex_font_glyph(custom.font_main, 'A', &glyphs[0]);
	bool have_b = tex_font_glyph(custom.font_main, 'B', &glyphs[1]);
	CHECK(have_a && have_b);
	int width = glyphs[0].advance > glyphs[1].advance ? glyphs[0].advance : glyphs[1].advance;
	const char* sources[] = { "A\nB", "A B" };
	for (size_t sample = 0; sample < sizeof(sources) / sizeof(sources[0]); ++sample)
	{
		TeX_Layout* layout = tex_format(sources[sample], width, &custom);
		TeX_Renderer* renderer = tex_renderer_create();
		CHECK(layout && renderer && tex_get_last_error(layout) == TEX_OK);
		CHECK(tex_get_total_height(layout) < GFX_LCD_HEIGHT);
		gfx_FillScreen(custom.palette[0]);
		tex_draw(renderer, layout, 10, 0, 0);
		CHECK(tex_get_last_error(layout) == TEX_OK && renderer->line_count == 2);
		CHECK(renderer->lines[0].y == 0);
		CHECK(renderer->lines[1].y == renderer->lines[0].h);
		CHECK(tex_get_total_height(layout) == renderer->lines[1].y + renderer->lines[1].h);

		size_t expected_ink = 0;
		for (int i = 0; i < 2; ++i)
		{
			const TexGlyph* glyph = &glyphs[i];
			const TeX_Line* line = &renderer->lines[i];
			int asc = -glyph->y_offset > tex_font_asc(custom.font_main)
			    ? -glyph->y_offset : tex_font_asc(custom.font_main);
			int desc = glyph->y_offset + glyph->height > tex_font_desc(custom.font_main)
			    ? glyph->y_offset + glyph->height : tex_font_desc(custom.font_main);
			CHECK(line->asc == asc && line->h == asc + desc + TEX_LINE_LEADING);
			TexListBlock* block = pool_get_list_block(&renderer->pool, line->content);
			Node* text = pool_get_node(&renderer->pool, block->items[0]);
			CHECK(text->type == N_TEXT && text->w == glyph->advance);
			int left = 10 + glyph->x_offset;
			int top = line->y + line->asc + glyph->y_offset;
			CHECK(top >= line->y && top + glyph->height <= line->y + line->h);

			size_t ink = 0;
			for (unsigned int row = 0; row < glyph->height; ++row)
			{
				for (unsigned int col = 0; col < glyph->width; ++col)
				{
					unsigned int index = row * glyph->width + col;
					unsigned int level = (glyph->pixels[index / 4] >> (6u - 2u * (index % 4))) & 3u;
					CHECK(gfx_vram[(top + (int)row) * GFX_LCD_WIDTH + left + (int)col] == custom.palette[level]);
					ink += level != 0;
				}
			}
			CHECK(ink > 0);
			expected_ink += ink;
		}

		size_t actual_ink = 0;
		for (size_t pixel = 0; pixel < (size_t)GFX_LCD_WIDTH * GFX_LCD_HEIGHT; ++pixel)
		{
			actual_ink += gfx_vram[pixel] != custom.palette[0];
		}
		CHECK(actual_ink == expected_ink);

		tex_renderer_destroy(renderer);
		tex_free(layout);
	}
	free(payload);
}

static int line_width(TeX_Renderer* renderer, const TeX_Line* line)
{
	int width = 0;
	for (ListId id = line->content; id != LIST_NULL;)
	{
		TexListBlock* block = pool_get_list_block(&renderer->pool, id);
		for (uint16_t i = 0; i < block->count; ++i)
		{
			width += pool_get_node(&renderer->pool, block->items[i])->w;
		}
		id = block->next;
	}
	return width;
}

static void check_justification(const TeX_Config* config)
{
	const struct
	{
		const char* source;
		const char* first_line;
		int extra;
		bool stretch;
	} cases[] = {
		{ "AA AA AA AA", "AA AA AA", 1, true },
		{ "AA   AA AA AA", "AA AA AA", 1, true },
		{ "AA AA AA\nAA", "AA AA AA", 1, false },
		{ "AA AA AA", "AA AA AA", 1, false },
		{ "AA AA AA   ", "AA AA AA", 1, false },
		{ "AA AA AA$$x$$", "AA AA AA", 1, false },
		{ "AAAAAAAAAA AA", "AAAAAAAAAA", -1, false },
		{ "AA $x\\,y$ AA AA", "AA $x\\,y$ AA", 1, true },
		{ "AA$xy$AA AA", "AA$xy$AA", 1, false },
		{ "A A A A A A A A A A A A A A A A A",
		  "A A A A A A A A A A A A A A A A", 7, true },
	};

	TeX_Renderer* renderer = tex_renderer_create();
	CHECK(renderer);
	TexGlyph space;
	CHECK(tex_font_glyph(config->font_main, ' ', &space));
	TeX_Config justified = *config;
	justified.justify = true;

	for (size_t sample = 0; sample < sizeof(cases) / sizeof(cases[0]); ++sample)
	{
		TeX_Layout* reference = tex_format(cases[sample].first_line, 300, config);
		CHECK(reference);
		draw_hash(renderer, reference, 0);
		CHECK(renderer->line_count == 1);
		int natural = line_width(renderer, &renderer->lines[0]);
		tex_free(reference);

		int width = natural + cases[sample].extra;
		TeX_Layout* plain = tex_format(cases[sample].source, width, config);
		CHECK(plain);
		draw_hash(renderer, plain, 0);
		CHECK(line_width(renderer, &renderer->lines[0]) == natural);
		int count = renderer->line_count;
		int last_width = line_width(renderer, &renderer->lines[count - 1]);
		int last_offset = renderer->lines[count - 1].x_offset;
		int height = tex_get_total_height(plain);
		tex_free(plain);

		TeX_Layout* layout = tex_format(cases[sample].source, width, &justified);
		CHECK(layout && tex_get_total_height(layout) == height);
		draw_hash(renderer, layout, 0);
		CHECK(renderer->line_count == count);
		CHECK(line_width(renderer, &renderer->lines[0]) ==
		      (cases[sample].stretch ? width : natural));
		CHECK(line_width(renderer, &renderer->lines[count - 1]) == last_width);
		CHECK(renderer->lines[count - 1].x_offset == last_offset);

		int min_space = width;
		int max_space = 0;
		for (ListId id = renderer->lines[0].content; id != LIST_NULL;)
		{
			TexListBlock* block = pool_get_list_block(&renderer->pool, id);
			for (uint16_t i = 0; i < block->count; ++i)
			{
				Node* node = pool_get_node(&renderer->pool, block->items[i]);
				if (node->type == N_SPACE)
				{
					if (node->w < min_space)
					{
						min_space = node->w;
					}
					if (node->w > max_space)
					{
						max_space = node->w;
					}
				}
			}
			id = block->next;
		}
		if (max_space)
		{
			CHECK(min_space >= space.advance && max_space - min_space <= 1);
			if (!cases[sample].stretch)
			{
				CHECK(max_space == space.advance);
			}
		}

		tex_free(layout);
	}
	tex_renderer_destroy(renderer);
}

int main(void)
{
	PortCE_initialize("libtexce layout regression");
	gfx_Begin();
	TeX_Config config = { .palette = {255, 254, 253, 0} };
	CHECK(tex_font_open(&config.font_main, "TeXFonts"));
	CHECK(tex_font_open(&config.font_script, "TeXScrpt"));

	for (size_t i = 0; i < 4; ++i)
	{
		for (size_t j = i + 1; j < 4; ++j)
		{
			TeX_Config invalid = config;
			invalid.palette[j] = invalid.palette[i];
			TeX_Layout* rejected = tex_format("x", 100, &invalid);
			CHECK(rejected && tex_get_last_error(rejected) == TEX_ERR_INPUT);
			tex_free(rejected);
		}
	}

	TeX_Config missing = config;
	missing.font_main = (TeX_Font){ 0 };
	TeX_Layout* rejected = tex_format("x", 100, &missing);
	CHECK(rejected && tex_get_last_error(rejected) == TEX_ERR_FONT);
	tex_free(rejected);

	missing = config;
	missing.font_script = (TeX_Font){ 0 };
	rejected = tex_format("x", 100, &missing);
	CHECK(rejected && tex_get_last_error(rejected) == TEX_ERR_FONT);
	tex_free(rejected);

	char words[1001];
	for (int i = 0; i < 200; ++i)
	{
		char* word = words + (size_t)i * 5;
		word[0] = 'w';
		word[1] = (char)('0' + i / 100);
		word[2] = (char)('0' + i / 10 % 10);
		word[3] = (char)('0' + i % 10);
		word[4] = ' ';
	}
	words[1000] = '\0';
	check_replay(words, 30, &config);

	const char block[] = "first \\% $x^2$ words\n\n$$\\frac{a}{b}$$\nlast $\\sqrt{x}$ ";
	char mixed[12 * (sizeof(block) - 1) + 1];
	for (size_t i = 0; i < 12; ++i)
	{
		memcpy(mixed + i * (sizeof(block) - 1), block, sizeof(block) - 1);
	}
	mixed[sizeof(mixed) - 1] = '\0';
	check_replay(mixed, 100, &config);
	CHECK(memcmp(mixed, block, sizeof(block) - 1) == 0);

	check_cache_and_fonts(&config);
	check_antialias_and_clipping(&config);
	check_accent_ink(&config);
	check_script_delimiter_clearance(&config);
	check_text_ink_bounds(&config);
	check_justification(&config);

	TeX_Config justified = config;
	justified.justify = true;
	check_replay(words, 110, &justified);
	check_replay(mixed, 100, &justified);

	TeX_Layout* layout = tex_format("word", 100, &config);
	TeX_Renderer* small = tex_renderer_create_sized(sizeof(Node) - 1);
	CHECK(layout && small && tex_get_last_error(layout) == TEX_OK);
	tex_draw(small, layout, 0, 0, 0);
	CHECK(tex_get_last_error(layout) == TEX_ERR_OOM);
	CHECK(small->window_y_end < tex_get_total_height(layout));
	tex_free(layout);
	tex_renderer_destroy(small);

	gfx_End();
	PortCE_terminate();
	puts("test_layout: PASS (checkpoint replay, cache lifetime, font switching)");
	return 0;
}
