#include <stdlib.h>
#include <string.h>

#include "tex.h"
#include "tex_internal.h"
#include "tex_measure.h"
#include "tex_metrics.h"
#include "tex_parse.h"
#include "tex_renderer.h"
#include "tex_token.h"
#include "tex_util.h"
#include "texfont.h"



static UnifiedPool* g_draw_pool = NULL;

#include <graphx.h>
#if defined(__TICE__)
#include <debug.h>
#endif

static int g_draw_vis_top = 0;
static int g_draw_vis_bot = TEX_VIEWPORT_H;
static int g_draw_x0 = 0;
static int g_draw_x1 = GFX_LCD_WIDTH;
static uint8_t* g_draw_pixels;
static const uint8_t* g_draw_palette;

// -------------------------
// drawing primitives
// -------------------------
static void blend_pixel(uint8_t* pixel, unsigned int level)
{
	if (!level)
		return;

	uint8_t old = *pixel;
	unsigned int previous = old == g_draw_palette[3] ? 3u :
	    old == g_draw_palette[2] ? 2u :
	    old == g_draw_palette[1] ? 1u : 0u;
	if (level > previous)
		*pixel = g_draw_palette[level];
}

static void rec_pixel(int x, int y, unsigned int level)
{
	if (x >= g_draw_x0 && x < g_draw_x1 && y >= g_draw_vis_top && y < g_draw_vis_bot)
		blend_pixel(g_draw_pixels + y * GFX_LCD_WIDTH + x, level);
}

static int draw_glyph(int x, int baseline, uint16_t code, TeX_Font font)
{
	TexGlyph glyph;
	if (!tex_font_glyph(font, code, &glyph))
		return 0;

	int x0 = x + glyph.x_offset;
	int y0 = baseline + glyph.y_offset;
	int left = TEX_MAX(x0, g_draw_x0);
	int right = TEX_MIN(x0 + glyph.width, g_draw_x1);
	int top = TEX_MAX(y0, g_draw_vis_top);
	int bottom = TEX_MIN(y0 + glyph.height, g_draw_vis_bot);
	if (left >= right || top >= bottom)
		return glyph.advance;

	// each byte holds 4 2 bit coverage values, most significant first
	static const uint8_t mask[4] = { 192, 48, 12, 3 };
	static const uint8_t middle[4] = { 128, 32, 8, 2 };
	for (int row = top; row < bottom; ++row)
	{
		unsigned int offset = (unsigned int)((row - y0) * glyph.width + left - x0);
		const uint8_t* bits = glyph.pixels + offset / 4;
		unsigned int part = offset % 4;
		uint8_t* dst = g_draw_pixels + row * GFX_LCD_WIDTH + left;

		for (int col = left; col < right; ++col, ++dst)
		{
			uint8_t value = (uint8_t)(*bits & mask[part]);
			if (value)
				blend_pixel(dst, value == mask[part] ? 3u : value == middle[part] ? 2u : 1u);

			if (++part == 4)
			{
				part = 0;
				++bits;
			}
		}
	}

	return glyph.advance;
}

static void rec_text(int x, int baseline, const char* s, int len, FontRole role)
{
	TeX_Font font = tex_metrics_font(role);

	for (int i = 0; i < len && s[i]; ++i)
		x += draw_glyph(x, baseline, (uint8_t)s[i], font);
}


static void rec_rule(int x, int y, int w)
{
	if (y < g_draw_vis_top || y >= g_draw_vis_bot)
		return;

	int left = TEX_MAX(x, g_draw_x0);
	int right = TEX_MIN(x + w, g_draw_x1);
	if (left < right)
		memset(g_draw_pixels + y * GFX_LCD_WIDTH + left, g_draw_palette[3], (size_t)(right - left));
}

static void rec_line(int x1, int y1, int x2, int y2)
{
	// Wu rasterization: split coverage between adjacent pixels, using
	// the same four levels as glyphs
	int steep = abs(y2 - y1) > abs(x2 - x1);

	// u follows the major axis; v follows the minor axis
	int u0 = steep ? y1 : x1;
	int u1 = steep ? y2 : x2;
	int v0 = steep ? x1 : y1;
	int v1 = steep ? x2 : y2;
	if (u0 > u1)
	{
		int tmp = u0;
		u0 = u1;
		u1 = tmp;

		tmp = v0;
		v0 = v1;
		v1 = tmp;
	}

	int major = u1 - u0;
	int minor = abs(v1 - v0);
	int direction = v1 < v0 ? -1 : 1;
	if (!major)
	{
		rec_pixel(x1, y1, 3);
		return;
	}

	int first = TEX_MAX(u0, steep ? g_draw_vis_top : g_draw_x0);
	int last = TEX_MIN(u1, (steep ? g_draw_vis_bot : g_draw_x1) - 1);
	if (first > last)
		return;

	long advance = (long)(first - u0) * minor;
	int v = v0 + direction * (int)(advance / major);
	int remainder = (int)(advance % major);

	// round fractional coverage into the four palettes
	int half = major / 2;
	int threshold_one = (major - half + 2) / 3;
	int threshold_two = (2 * major - half + 2) / 3;
	int threshold_three = (3 * major - half + 2) / 3;

	for (int u = first; u <= last; ++u)
	{
		unsigned int shade = remainder >= threshold_three ? 3u :
		    remainder >= threshold_two ? 2u :
		    remainder >= threshold_one ? 1u : 0u;
		rec_pixel(steep ? v : u, steep ? u : v, 3u - shade);
		if (shade)
			rec_pixel(steep ? v + direction : u, steep ? u : v + direction, shade);

		remainder += minor;
		if (remainder >= major)
		{
			remainder -= major;
			v += direction;
		}
	}
}

static void rec_dot(int cx, int cy)
{
	rec_pixel(cx, cy, 3);
	rec_pixel(cx - 1, cy, 2);
	rec_pixel(cx + 1, cy, 2);
	rec_pixel(cx, cy - 1, 2);
	rec_pixel(cx, cy + 1, 2);
}


// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void rec_draw_paren(int x, int y_center, int w, int h, int is_left)
{
	if (h <= 0 || w <= 0)
		return;

	int ry = h / 2; // vertical radius (half height)
	int rx = w - 1; // horizontal radius

	static const int cos_table[7] = { 0, 128, 221, 256, 221, 128, 0 };
	static const int sin_table[7] = { -256, -221, -128, 0, 128, 221, 256 };

	int cx;
	int prev_px;
	int prev_py;

	if (is_left)
	{
		cx = x + w - 1;
		prev_px = cx - (rx * cos_table[0]) / 256;
		prev_py = y_center + (ry * sin_table[0]) / 256;
	}
	else
	{
		cx = x; // center at left edge
		prev_px = cx + (rx * cos_table[0]) / 256;
		prev_py = y_center + (ry * sin_table[0]) / 256;
	}

	for (int i = 1; i < 7; i++)
	{
		int cur_px;
		int cur_py;
		cur_py = y_center + (ry * sin_table[i]) / 256;

		if (is_left)
			cur_px = cx - (rx * cos_table[i]) / 256;
		else
			cur_px = cx + (rx * cos_table[i]) / 256;

		rec_line(prev_px, prev_py, cur_px, cur_py);
		prev_px = cur_px;
		prev_py = cur_py;
	}
}

static void rec_ellipse(int cx, int cy, int rx, int ry)
{
	if (rx < 0 || ry < 0)
		return;

	rec_draw_paren(cx - rx, cy, rx + 1, ry * 2, 1);
	rec_draw_paren(cx, cy, rx + 1, ry * 2, 0);
}


void tex_draw_log_reset(void)
{
}

int tex_draw_log_count(void)
{
	return 0;
}
int tex_draw_log_get(TexDrawOp* out, int max)
{
	(void)out;
	(void)max;
	return 0;
}
int tex_draw_log_get_range(TexDrawOp* out, int start, int count)
{
	(void)out;
	(void)start;
	(void)count;
	return 0;
}

// -------------------------
// Node draw routines
// -------------------------
static void draw_node(Node* node, TexCoord x, TexBaseline baseline_y);

static void draw_math_list(ListId head, TexCoord x, TexBaseline baseline_y, FontRole role)
{
	TexMathCursor cursor;
	tex_math_begin(&cursor, head, role);
	int offset;
	Node* node;
	while ((node = tex_math_next(g_draw_pool, &cursor, &offset)))
		draw_node(node, (TexCoord){x.v + offset}, baseline_y);
}

static void draw_script(Node* node, TexCoord x, TexBaseline baseline_y)
{
	TexScriptGeometry geometry;
	tex_measure_script_geometry(g_draw_pool, node, &geometry);

	draw_node(pool_get_node(g_draw_pool, node->data.script.base),
	    (TexCoord){ x.v + geometry.base_x }, baseline_y);
	draw_node(pool_get_node(g_draw_pool, node->data.script.sup),
	    (TexCoord){ x.v + geometry.sup_x }, (TexBaseline){ baseline_y.v + geometry.sup_y });
	draw_node(pool_get_node(g_draw_pool, node->data.script.sub),
	    (TexCoord){ x.v + geometry.sub_x }, (TexBaseline){ baseline_y.v + geometry.sub_y });
}

static void draw_frac(Node* node, TexCoord x, TexBaseline baseline_y)
{
	TexFracGeometry geometry;
	tex_measure_frac_geometry(g_draw_pool, node, &geometry);

	for (int row = 0; row < geometry.rule; ++row)
		rec_rule(x.v + geometry.bar_x, baseline_y.v + geometry.bar_y + row, geometry.bar_w);

	draw_node(pool_get_node(g_draw_pool, node->data.frac.num),
	    (TexCoord){ x.v + geometry.num_x }, (TexBaseline){ baseline_y.v + geometry.num_y });
	draw_node(pool_get_node(g_draw_pool, node->data.frac.den),
	    (TexCoord){ x.v + geometry.den_x }, (TexBaseline){ baseline_y.v + geometry.den_y });
}

static void draw_sqrt(Node* node, TexCoord x, TexBaseline baseline_y)
{
	TexSqrtGeometry geometry;
	tex_measure_sqrt_geometry(g_draw_pool, node, &geometry);

	int left = x.v + geometry.head_x;
	int join = left + geometry.head_w;
	int top = baseline_y.v + geometry.bar_y;
	int bottom = baseline_y.v + geometry.bottom_y;
	int height = bottom - top;

	rec_line(left, bottom - height / 4, left + geometry.head_w / 4, bottom - height / 3);
	rec_line(left + geometry.head_w / 4, bottom - height / 3, left + geometry.head_w / 2, bottom);
	rec_line(left + geometry.head_w / 2, bottom, join, top);

	for (int row = 0; row < geometry.rule; ++row)
		rec_rule(join, top + row, geometry.w - geometry.head_x - geometry.head_w);

	draw_node(pool_get_node(g_draw_pool, node->data.sqrt.rad),
	    (TexCoord){ x.v + geometry.rad_x }, baseline_y);
	draw_node(pool_get_node(g_draw_pool, node->data.sqrt.index),
	    (TexCoord){ x.v + geometry.index_x }, (TexBaseline){ baseline_y.v + geometry.index_y });
}

static void draw_overlay(Node* node, TexCoord x, TexBaseline baseline_y, FontRole role)
{
	Node* base = pool_get_node(g_draw_pool, node->data.overlay.base);

	draw_node(base, x, baseline_y);

	int top = baseline_y.v - base->asc - TEX_ACCENT_GAP;

	const Node* nucleus = base;
	while (nucleus->type == N_MATH)
	{
		TexListBlock* block = pool_get_list_block(g_draw_pool, nucleus->data.list.head);
		if (!block || block->count != 1 || block->next != LIST_NULL)
			break;
		nucleus = pool_get_node(g_draw_pool, block->items[0]);
	}
	TexGlyph base_glyph;
	int center = x.v +
	    (nucleus->type == N_GLYPH && tex_metrics_glyph(nucleus, &base_glyph) ?
	     base_glyph.top_accent : base->w / 2);

	switch (node->data.overlay.type)
	{
	case ACC_BAR:
		{
			int bar_y = top - 1;
			int pad = (base->w > 2) ? 1 : 0;
			rec_line(x.v + pad, bar_y, x.v + base->w - 1 - pad, bar_y);
		}
		break;

	case ACC_OVERLINE:
		{
			int line_y = top - 1;
			int pad = (base->w > 2) ? 1 : 0;
			rec_line(x.v + pad, line_y, x.v + base->w - 1 - pad, line_y);
		}
		break;

	case ACC_UNDERLINE:
		{
			int line_y = baseline_y.v + base->desc + TEX_ACCENT_GAP;
			int pad = (base->w > 2) ? 1 : 0;
			rec_line(x.v + pad, line_y, x.v + base->w - 1 - pad, line_y);
		}
		break;

	case ACC_DOT:
		{
			int cx = center;
			rec_dot(cx, top - 1);
		}
		break;

	case ACC_HAT:
		{
			int cx = center;
			int dy = 3;
			rec_line(cx - dy, top, cx, top - dy);
			rec_line(cx, top - dy, cx + dy, top);
		}
		break;

	case ACC_VEC:
		{
			int len = TEX_MAX(5, base->w);
			int x_end = center + (len + 1) / 2;
			int x_start = x_end - len;
			int y = top - 2;

			rec_line(x_start, y, x_end, y);
			rec_line(x_end - 3, y - 2, x_end, y);
			rec_line(x_end - 3, y + 2, x_end, y);
		}
		break;

	case ACC_DDOT:
		{
			int cx = center;
			int sep = 2;
			rec_dot(cx - sep, top - 1);
			rec_dot(cx + sep, top - 1);
		}
		break;

	case ACC_TILDE:
		{
			TeX_Font font = tex_metrics_font(role);
			TexGlyph glyph;
			if (tex_font_glyph(font, '~', &glyph))
			{
				int cx = center - glyph.width / 2 - glyph.x_offset;
				(void)draw_glyph(cx, top - glyph.height - glyph.y_offset, '~', font);
			}
		}
		break;

	default:
		break;
	}
}

static void draw_hbrace(int x, int y, int w, int is_over)
{
	if (w <= 0)
		return;
	if (w < 6)
	{
		rec_line(x, y + (is_over ? (TEX_BRACE_HEIGHT - 2) : 1), x + w - 1, y + (is_over ? (TEX_BRACE_HEIGHT - 2) : 1));
		return;
	}
	int mid = x + w / 2;
	int arm_h = 2;
	int top_y = y;
	if (is_over)
	{
		int base_y = top_y + arm_h;
		rec_line(x, base_y, mid - 2, base_y);
		rec_line(mid - 2, base_y, mid, top_y);
		rec_line(mid, top_y, mid + 2, base_y);
		rec_line(mid + 2, base_y, x + w - 1, base_y);
	}
	else
	{
		int base_y = y;
		rec_line(x, base_y, mid - 2, base_y);
		rec_line(mid - 2, base_y, mid, base_y + arm_h + (TEX_BRACE_HEIGHT - arm_h));
		rec_line(mid, base_y + arm_h + (TEX_BRACE_HEIGHT - arm_h), mid + 2, base_y);
		rec_line(mid + 2, base_y, x + w - 1, base_y);
	}
}

static void draw_spandeco(Node* node, TexCoord x, TexBaseline baseline_y)
{
	Node* content = pool_get_node(g_draw_pool, node->data.spandeco.content);
	Node* label = pool_get_node(g_draw_pool, node->data.spandeco.label);
	draw_node(content, x, baseline_y);
	int w = content->w;
	int bh = TEX_BRACE_HEIGHT;
	if (node->data.spandeco.deco_type == DECO_OVERBRACE)
	{
		int brace_y = baseline_y.v - content->asc - TEX_ACCENT_GAP - bh + 1;
		draw_hbrace(x.v, brace_y, w, 1);
		if (label)
		{
			TexCoord label_x = { x.v + (w - label->w) / 2 };
			TexBaseline label_bl = { brace_y - TEX_ACCENT_GAP - label->desc };
			draw_node(label, label_x, label_bl);
		}
	}
	else
	{
		int ub_gap = TEX_ACCENT_GAP + 2; // extra space above underbrace for tall delimiters
		int brace_y = baseline_y.v + content->desc + ub_gap;
		draw_hbrace(x.v, brace_y, w, 0);
		if (label)
		{
			TexCoord label_x = { x.v + (w - label->w) / 2 };
			TexBaseline label_bl = { brace_y + bh + TEX_ACCENT_GAP + label->asc };
			draw_node(label, label_x, label_bl);
		}
	}
}

static void draw_multiop(Node* node, TexCoord x, TexBaseline baseline_y)
{
	TexGlyph glyph;
	(void)tex_metrics_glyph(node, &glyph);
	FontRole role = (node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN;
	TeX_Font font = tex_metrics_font(role);
	uint16_t code = tex_font_math_code((uint8_t)TEXFONT_INTEGRAL_CHAR, (node->flags & TEX_FLAG_MATHF_DISPLAY) != 0);

	for (uint8_t i = 0; i < node->data.multiop.count; ++i)
		(void)draw_glyph(x.v + i * glyph.advance, baseline_y.v, code, font);

	if (node->data.multiop.op_type == MULTIOP_OINT)
	{
		int cx = x.v + (node->w - 1) / 2;
		int cy = baseline_y.v - tex_font_metric(font, TEX_FONT_AXIS);
		rec_ellipse(cx, cy, node->w / 2, glyph.advance / 2);
	}
}


// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void draw_proc_delim(int x, int y_center, int h, DelimType type, int is_left, const TexDelimiterGeometry* geometry)
{
	x += geometry->ink_x;
	int w = geometry->ink_w;

	int top = y_center - h / 2;
	int bot = y_center + h / 2;

	switch (type)
	{
	case DELIM_NONE:
		break;
	case DELIM_PAREN:
		rec_draw_paren(x, y_center, w, h, is_left);
		break;
	case DELIM_BRACKET:
		if (is_left)
		{
			rec_line(x, top, x, bot);
			rec_line(x, top, x + (w - 1) / 2, top);
			rec_line(x, bot, x + (w - 1) / 2, bot);
		}
		else
		{
			rec_line(x + w - 1, top, x + w - 1, bot);
			rec_line(x + w / 2, top, x + w - 1, top);
			rec_line(x + w / 2, bot, x + w - 1, bot);
		}
		break;
	case DELIM_BRACE:
		{
			const int LUT_X[] = { 0, 81, 135, 157, 160, 156, 152, 152, 156, 161, 166, 167, 165, 164, 170, 198, 256 };
			const int LUT_Y[] = { 0, 11, 26, 42, 59, 76, 92, 109, 125, 142, 159, 175, 192, 208, 225, 241, 256 };
			const int lut_steps = sizeof(LUT_Y) / sizeof(LUT_Y[0]);

			int half_h = h / 2;
			int prev_px;
			int prev_py;

			// beak setup
			int beak_x = is_left ? x : (x + w - 1);

			prev_px = beak_x;
			prev_py = y_center;

			// top half (center -> top)
			for (int i = 1; i < lut_steps; i++)
			{
				int dy = (LUT_Y[i] * half_h) >> 8;
				int dx = (LUT_X[i] * (w - 1)) >> 8;

				int cur_py = y_center - dy;
				int cur_px = is_left ? (beak_x + dx) : (beak_x - dx);

				rec_line(prev_px, prev_py, cur_px, cur_py);
				prev_px = cur_px;
				prev_py = cur_py;
			}

			// bottom half (center -> bottom)
			prev_px = beak_x;
			prev_py = y_center;

			for (int i = 1; i < lut_steps; i++)
			{
				int dy = (LUT_Y[i] * half_h) >> 8;
				int dx = (LUT_X[i] * (w - 1)) >> 8;

				int cur_py = y_center + dy;
				int cur_px = is_left ? (beak_x + dx) : (beak_x - dx);

				rec_line(prev_px, prev_py, cur_px, cur_py);
				prev_px = cur_px;
				prev_py = cur_py;
			}
		}
		break;
	case DELIM_VERT:
		{
			int vx = is_left ? x : (x + w - 1);
			rec_line(vx, top, vx, bot);
		}
		break;
	case DELIM_ANGLE:
		if (is_left)
		{
			rec_line(x + w - 1, top, x, y_center);
			rec_line(x, y_center, x + w - 1, bot);
		}
		else
		{
			rec_line(x, top, x + w - 1, y_center);
			rec_line(x + w - 1, y_center, x, bot);
		}
		break;
	case DELIM_FLOOR:
		if (is_left)
		{
			rec_line(x, top, x, bot);
			rec_line(x, bot, x + (w - 1) / 2, bot);
		}
		else
		{
			rec_line(x + w - 1, top, x + w - 1, bot);
			rec_line(x + w / 2, bot, x + w - 1, bot);
		}
		break;
	case DELIM_CEIL:
		if (is_left)
		{
			rec_line(x, top, x, bot);
			rec_line(x, top, x + (w - 1) / 2, top);
		}
		else
		{
			rec_line(x + w - 1, top, x + w - 1, bot);
			rec_line(x + w / 2, top, x + w - 1, top);
		}
		break;
	default:
		break;
	}
}

static void draw_auto_delim(Node* node, TexCoord x, TexBaseline baseline_y, FontRole role)
{
	int h = node->data.auto_delim.delim_h;
	int axis = tex_font_metric(tex_metrics_font(role), TEX_FONT_AXIS);
	int y_center = baseline_y.v - axis;

	TexDelimiterGeometry left;
	TexDelimiterGeometry right;
	tex_measure_delimiter_geometry((DelimType)node->data.auto_delim.left_type, true, h, role, &left);
	tex_measure_delimiter_geometry((DelimType)node->data.auto_delim.right_type, false, h, role, &right);

	if (left.advance > 0)
		draw_proc_delim(x.v, y_center, h, (DelimType)node->data.auto_delim.left_type, 1, &left);

	ListId content_list = node->data.auto_delim.content;
	if (content_list != LIST_NULL)
	{
		TexCoord content_x = { x.v + left.advance };
		draw_math_list(content_list, content_x, baseline_y, role);
	}

	if (right.advance > 0)
	{
		int rx = x.v + node->w - right.advance;
		draw_proc_delim(rx, y_center, h, (DelimType)node->data.auto_delim.right_type, 0, &right);
	}
}

static void draw_matrix(Node* node, TexCoord x, TexBaseline baseline_y)
{
	TexMatrixGeometry geometry;
	tex_measure_matrix_geometry(g_draw_pool, node, &geometry);
	int16_t y_center = (int16_t)(baseline_y.v - geometry.axis);

	// left delimiter
	if (node->data.matrix.delim_type != DELIM_NONE)
	{
		draw_proc_delim(x.v, y_center, geometry.content_h, (DelimType)node->data.matrix.delim_type, 1, &geometry.left);
	}

	// right delimiter
	if (node->data.matrix.delim_type != DELIM_NONE)
	{
		int16_t rx = (int16_t)(x.v + geometry.left.advance + geometry.content_w);
		draw_proc_delim(rx, y_center, geometry.content_h, (DelimType)node->data.matrix.delim_type, 0, &geometry.right);
	}

	// cells
	int16_t content_x = (int16_t)(x.v + geometry.left.advance);
	int16_t content_y_top = (int16_t)(y_center - geometry.content_h / 2);

	int16_t cur_y = content_y_top;
	TexListBlock* block = pool_get_list_block(g_draw_pool, node->data.matrix.cells);
	uint16_t item = 0;

	for (uint8_t r = 0; r < geometry.rows; r++)
	{
		int16_t row_baseline = (int16_t)(cur_y + geometry.row_ascs[r]);
		int16_t cur_x = content_x;

		for (uint8_t c = 0; c < geometry.cols; c++)
		{
			while (block && item >= block->count)
			{
				block = pool_get_list_block(g_draw_pool, block->next);
				item = 0;
			}
			NodeRef cell_ref = block ? block->items[item++] : NODE_NULL;

			Node* cell = pool_get_node(g_draw_pool, cell_ref);
			if (cell)
			{
				// center cell horizontally within column
				int16_t cell_x = (int16_t)(cur_x + (geometry.col_widths[c] - cell->w) / 2);
				draw_node(cell, (TexCoord){ cell_x }, (TexBaseline){ row_baseline });
			}

			cur_x = (int16_t)(cur_x + geometry.col_widths[c] + TEX_MATRIX_COL_SPACING);
			// add extra padding if there's a separator after this column
			if (node->data.matrix.col_separators & (1 << c))
				cur_x = (int16_t)(cur_x + 2 * TEX_MATRIX_SEP_PAD);
		}

		cur_y = (int16_t)(cur_y + geometry.row_ascs[r] + geometry.row_descs[r] + TEX_MATRIX_ROW_SPACING);
	}

	// draw column separators (for array environment)
	if (node->data.matrix.col_separators != 0)
	{
		int16_t sep_x = content_x;
		for (uint8_t c = 0; c < geometry.cols; c++)
		{
			sep_x = (int16_t)(sep_x + geometry.col_widths[c]);
			if (node->data.matrix.col_separators & (1 << c))
			{
				// draw vertical line centered in the gap (normal spacing + separator padding)
				int16_t line_x = (int16_t)(sep_x + TEX_MATRIX_COL_SPACING / 2 + TEX_MATRIX_SEP_PAD);
				rec_line(line_x, content_y_top, line_x, content_y_top + geometry.content_h);
				sep_x = (int16_t)(sep_x + TEX_MATRIX_COL_SPACING + 2 * TEX_MATRIX_SEP_PAD);
			}
			else
			{
				sep_x = (int16_t)(sep_x + TEX_MATRIX_COL_SPACING);
			}
		}
	}
}

static void draw_node(Node* node, TexCoord x, TexBaseline baseline_y)
{
	if (!node)
		return;

	FontRole role = (node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN;

	switch (node->type)
	{
	case N_TEXT:
		{
			const char* s = pool_get_string(g_draw_pool, node->data.text.sid);
			int len = node->data.text.len;
			rec_text(x.v, baseline_y.v, s, len, role);
		}
		break;
	case N_GLYPH:
		{
			uint16_t code = tex_font_math_code((uint8_t)node->data.glyph, (node->flags & TEX_FLAG_MATHF_DISPLAY) != 0);
			(void)draw_glyph(x.v, baseline_y.v, code, tex_metrics_font(role));
		}
		break;
	case N_SPACE:
		break;
	case N_MATH:
		draw_math_list(node->data.list.head, x, baseline_y, role);
		break;
	case N_SCRIPT:
		draw_script(node, x, baseline_y);
		break;
	case N_FRAC:
		draw_frac(node, x, baseline_y);
		break;
	case N_SQRT:
		draw_sqrt(node, x, baseline_y);
		break;
	case N_OVERLAY:
		draw_overlay(node, x, baseline_y, role);
		break;
	case N_SPANDECO:
		draw_spandeco(node, x, baseline_y);
		break;
	case N_MULTIOP:
		draw_multiop(node, x, baseline_y);
		break;
	case N_AUTO_DELIM:
		draw_auto_delim(node, x, baseline_y, role);
		break;
	case N_MATRIX:
		draw_matrix(node, x, baseline_y);
		break;
	default:
		break;
	}
}

static int find_checkpoint_index(TeX_Layout* L, int target_y)
{
	if (!L->checkpoints || L->checkpoint_count == 0)
		return -1;

	int lo = 0;
	int hi = L->checkpoint_count;
	while (lo < hi)
	{
		int mid = (lo + hi) / 2;
		if (L->checkpoints[mid].y_pos <= target_y)
			lo = mid + 1;
		else
			hi = mid;
	}

	return (lo > 0) ? (lo - 1) : -1;
}

static void rehydrate_window(TeX_Renderer* r, TeX_Layout* layout, int top, int bottom)
{
	int padded_top = TEX_MAX(0, top - TEX_RENDERER_PADDING);
	int padded_bot = TEX_MIN(layout->total_height, bottom + TEX_RENDERER_PADDING);

	tex_renderer_invalidate(r);
	if (layout->renderer)
		tex_renderer_invalidate(layout->renderer);

	r->cached_layout = layout;
	layout->renderer = r;


	int cp_idx = find_checkpoint_index(layout, padded_top);
	const char* source = cp_idx >= 0 ? layout->checkpoints[cp_idx].src_ptr : layout->source;
	int y = cp_idx >= 0 ? layout->checkpoints[cp_idx].y_pos : 0;
	TeX_Flow flow;
	tex_flow_init(&flow, layout, source, y);

	r->window_y_start = y;
	while (flow.y < padded_bot && r->line_count < TEX_RENDERER_MAX_LINES)
	{
		if (!tex_flow_next(&flow, layout, &r->pool, &r->lines[r->line_count], 1))
			break;
		r->line_count++;
	}

	r->window_y_end = flow.y;
	if (r->line_count == TEX_RENDERER_MAX_LINES && flow.y < bottom)
		TEX_SET_ERROR(layout, TEX_ERR_OOM, "Renderer line capacity exhausted", flow.y);
}

static void set_draw_palette(const uint8_t* palette)
{
	g_draw_palette = palette;
	uint16_t background = gfx_palette[palette[0]];
	uint16_t ink = gfx_palette[palette[3]];

	for (unsigned int shade = 1; shade < 3; ++shade)
	{
		uint16_t color = 0;
		for (unsigned int shift = 0; shift <= 10; shift += 5)
		{
			unsigned int background_component = ((unsigned int)background >> shift) & 31u;
			unsigned int ink_component = ((unsigned int)ink >> shift) & 31u;
			color |= (uint16_t)(((background_component * (3u - shade) + ink_component * shade) / 3u) << shift);
		}
		gfx_palette[palette[shade]] = color;
	}
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void tex_draw(TeX_Renderer* r, TeX_Layout* layout, int x, int y, int scroll_y)
{
	if (!r || !layout || !layout->fonts.main_font.data || !layout->fonts.script_font.data)
		return;

	tex_metrics_activate(&layout->fonts);

	int vis_top = y;
	if (vis_top < 0)
		vis_top = 0;
	if (vis_top > TEX_VIEWPORT_H)
		vis_top = TEX_VIEWPORT_H;

	int vis_bot = TEX_VIEWPORT_H;
	g_draw_vis_top = vis_top;
	g_draw_vis_bot = vis_bot;
	gfx_region_t clip = { 0, vis_top, GFX_LCD_WIDTH, vis_bot };
	if (!gfx_GetClipRegion(&clip))
		return;

	g_draw_x0 = (int)clip.xmin;
	g_draw_x1 = (int)clip.xmax;
	g_draw_vis_top = TEX_MAX(vis_top, (int)clip.ymin);
	g_draw_vis_bot = TEX_MIN(vis_bot, (int)clip.ymax);

	int viewport_top = TEX_CLAMP(scroll_y + vis_top - y, 0, layout->total_height);
	int viewport_bot = TEX_CLAMP(scroll_y + vis_bot - y, 0, layout->total_height);

	int hit = (r->cached_layout == layout) && (viewport_top >= r->window_y_start) && (viewport_bot <= r->window_y_end);

	if (!hit)
	{
		rehydrate_window(r, layout, viewport_top, viewport_bot);
	}

	// pool context for draw functions
	g_draw_pool = &r->pool;
	gfx_Wait();
	g_draw_pixels = &gfx_vbuffer[0][0];
	set_draw_palette(layout->cfg.palette);

	for (int i = 0; i < r->line_count; i++)
	{
		TeX_Line* ln = &r->lines[i];
		int line_screen_top = y + (ln->y - scroll_y);
		int line_screen_bot = line_screen_top + ln->h;

		if (line_screen_bot <= vis_top)
			continue;
		if (line_screen_top >= vis_bot)
			break;

		int baseline = line_screen_top + ln->asc;
		TexBaseline baseline_y = { baseline };


		// draw all nodes in the line content, calculating x positions on-the-fly
		int cur_x = x + ln->x_offset; // x_offset provides centering for display math
		for (ListId block_id = ln->content; block_id != LIST_NULL;)
		{
			TexListBlock* block = pool_get_list_block(g_draw_pool, block_id);
			for (uint16_t j = 0; j < block->count; j++)
			{
				Node* node = pool_get_node(g_draw_pool, block->items[j]);
				TexCoord node_x = { cur_x };
				draw_node(node, node_x, baseline_y);
				cur_x += node->w;
			}
			block_id = block->next;
		}

		if (r->cached_layout && ln->y + ln->h > r->window_y_end)
			break;
	}

	g_draw_pool = NULL;
	g_draw_vis_top = 0;
	g_draw_vis_bot = TEX_VIEWPORT_H;
}
