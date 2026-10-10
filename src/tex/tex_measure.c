#include "tex_measure.h"
#include "tex_internal.h"
#include "tex_metrics.h"
#include "tex_util.h"
#include "texfont.h"


static TexAtom atom_class(UnifiedPool* pool, const Node* node)
{
	while (node->type == N_SCRIPT || node->type == N_OVERLAY)
		node = pool_get_node(pool, node->type == N_SCRIPT ? node->data.script.base : node->data.overlay.base);

	if (node->type == N_SPACE)
		return TEX_ATOM_NONE;
	if ((node->flags & TEX_FLAG_OPERATOR) || node->type == N_MULTIOP)
		return TEX_ATOM_OP;
	if (node->type == N_FRAC || node->type == N_AUTO_DELIM || node->type == N_MATRIX)
		return TEX_ATOM_INNER;
	if (node->type != N_GLYPH)
		return TEX_ATOM_ORD;
	if (tex_is_big_operator(node->data.glyph))
		return TEX_ATOM_OP;

	switch (node->data.glyph)
	{
	case '+':
	case '-':
	case '*':
	case (uint8_t)TEXFONT_UNION_CHAR:
	case (uint8_t)TEXFONT_INTERSECTION_CHAR:
	case (uint8_t)TEXFONT_RING_OP_CHAR:
	case (uint8_t)TEXFONT_O_PLUS_CHAR:
	case (uint8_t)TEXFONT_PLUS_MINUS_CHAR:
	case (uint8_t)TEXFONT_MINUS_PLUS_CHAR:
	case (uint8_t)TEXFONT_DOT_OP_CHAR:
	case (uint8_t)TEXFONT_TIMES_CHAR:
	case (uint8_t)TEXFONT_DIVIDE_CHAR:
		return TEX_ATOM_BIN;
	case '=':
	case '<':
	case '>':
	case ':':
	case (uint8_t)TEXFONT_NOT_IN_CHAR:
	case (uint8_t)TEXFONT_SUBSET_EQ_CHAR:
	case (uint8_t)TEXFONT_EQUIVALENT_CHAR:
	case (uint8_t)TEXFONT_SIMILAR_CHAR:
	case (uint8_t)TEXFONT_CONGRUENT_CHAR:
	case (uint8_t)TEXFONT_PROPORTIONAL_CHAR:
	case (uint8_t)TEXFONT_PERPENDICULAR_CHAR:
	case (uint8_t)TEXFONT_PARALLEL_CHAR:
	case (uint8_t)TEXFONT_APPROX_CHAR:
	case (uint8_t)TEXFONT_NOT_EQUAL_CHAR:
	case (uint8_t)TEXFONT_LESS_EQUAL_CHAR:
	case (uint8_t)TEXFONT_GREATER_EQUAL_CHAR:
	case (uint8_t)TEXFONT_ARROW_RIGHT_CHAR:
	case (uint8_t)TEXFONT_ARROW_LEFT_CHAR:
	case (uint8_t)TEXFONT_ELEMENT_OF_CHAR:
	case (uint8_t)TEXFONT_SUBSET_OF_CHAR:
		return TEX_ATOM_REL;
	case '(':
	case '[':
	case '{':
	case (uint8_t)TEXFONT_LANGLE_CHAR:
		return TEX_ATOM_OPEN;
	case ')':
	case ']':
	case '}':
	case (uint8_t)TEXFONT_RANGLE_CHAR:
		return TEX_ATOM_CLOSE;
	case ',':
	case ';':
		return TEX_ATOM_PUNCT;
	default:
		return TEX_ATOM_ORD;
	}
}

static Node* list_next(UnifiedPool* pool, ListId* id, uint16_t* item)
{
	while (*id != LIST_NULL)
	{
		TexListBlock* block = pool_get_list_block(pool, *id);
		if (*item < block->count)
			return pool_get_node(pool, block->items[(*item)++]);

		*id = block->next;
		*item = 0;
	}

	return NULL;
}

void tex_math_begin(TexMathCursor* cursor, ListId head, FontRole role)
{
	*cursor = (TexMathCursor)
	{
		.block = head,
		.em = tex_font_metric(tex_metrics_font(role), TEX_FONT_EM),
		.previous = TEX_ATOM_NONE,
		.script = role == FONTROLE_SCRIPT
	};
}

int tex_math_position(const TexMathCursor* cursor)
{
	return (cursor->position_mu + (cursor->position_mu < 0 ? -9 : 9)) / 18;
}

Node* tex_math_next(UnifiedPool* pool, TexMathCursor* cursor, int* x)
{
	Node* node = list_next(pool, &cursor->block, &cursor->item);
	if (!node)
		return NULL;

	TexAtom current = atom_class(pool, node);
	TexAtom previous = cursor->previous;
	if (current == TEX_ATOM_BIN)
	{
		ListId id = cursor->block;
		uint16_t item = cursor->item;
		TexAtom next = TEX_ATOM_NONE;
		Node* following;
		while (next == TEX_ATOM_NONE && (following = list_next(pool, &id, &item)))
			next = atom_class(pool, following);
		if (previous == TEX_ATOM_NONE || previous == TEX_ATOM_BIN || previous == TEX_ATOM_OP ||
		    previous == TEX_ATOM_REL || previous == TEX_ATOM_OPEN || previous == TEX_ATOM_PUNCT ||
		    next == TEX_ATOM_NONE || next == TEX_ATOM_REL || next == TEX_ATOM_CLOSE || next == TEX_ATOM_PUNCT)
			current = TEX_ATOM_ORD;
	}

	if (current != TEX_ATOM_NONE && previous != TEX_ATOM_NONE)
	{
		// TeX atom spacing in mu (1/18 em)
		static const int8_t spacing[8][8] =
		{
			{ 0, 3,-4,-5, 0, 0, 0,-3 },
			{ 3, 3, 0,-5, 0, 0, 0,-3 },
			{-4,-4, 0, 0,-4, 0, 0,-4 },
			{-5,-5, 0, 0,-5, 0, 0,-5 },
			{ 0, 0, 0, 0, 0, 0, 0, 0 },
			{ 0, 3,-4,-5, 0, 0, 0,-3 },
			{-3,-3, 0,-3,-3,-3,-3,-3 },
			{-3, 3,-4,-5,-3, 0,-3,-3 }
		};
		int mu = spacing[previous][current];
		if (mu < 0)
			mu = cursor->script ? 0 : -mu;
		cursor->position_mu += mu * cursor->em;
	}

	*x = tex_math_position(cursor);

	cursor->position_mu += node->type == N_SPACE ? node->data.space.mu * cursor->em : node->w * 18;

	if (current != TEX_ATOM_NONE)
		cursor->previous = current;

	return node;
}

void tex_measure_delimiter_geometry(DelimType type, bool left, int height, FontRole role, TexDelimiterGeometry* geometry)
{
	*geometry = (TexDelimiterGeometry){ 0 };
	uint8_t code;
	switch (type)
	{
	case DELIM_PAREN:
		code = left ? '(' : ')';
		break;
	case DELIM_BRACE:
		code = left ? '{' : '}';
		break;
	case DELIM_VERT:
		code = '|';
		break;
	case DELIM_ANGLE:
		code = (uint8_t)(left ? TEXFONT_LANGLE_CHAR : TEXFONT_RANGLE_CHAR);
		break;
	case DELIM_BRACKET:
	case DELIM_FLOOR:
	case DELIM_CEIL:
		code = left ? '[' : ']';
		break;
	case DELIM_NONE:
	default:
		return;
	}
	TexGlyph glyph;

	(void)tex_font_glyph(tex_metrics_font(role), code, &glyph);
	TEX_COORD_ASSIGN(geometry->ink_x, TEX_MAX(0, glyph.x_offset));
	TEX_COORD_ASSIGN(geometry->ink_w, TEX_CLAMP(height / TEX_DELIM_WIDTH_FACTOR, TEX_DELIM_MIN_WIDTH, TEX_DELIM_MAX_WIDTH));
	TEX_COORD_ASSIGN(geometry->advance, geometry->ink_x + geometry->ink_w +
	    TEX_MAX(0, glyph.advance - glyph.x_offset - glyph.width));
}

void tex_measure_matrix_geometry(UnifiedPool* pool, const Node* node, TexMatrixGeometry* geometry)
{
	*geometry = (TexMatrixGeometry){ 0 };
	FontRole role = (node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN;
	TeX_Font font = tex_metrics_font(role);

	geometry->rows = TEX_MIN(node->data.matrix.rows, TEX_MATRIX_MAX_DIMS);
	geometry->cols = TEX_MIN(node->data.matrix.cols, TEX_MATRIX_MAX_DIMS);
	if (geometry->cols == 0)
		geometry->cols = 1;

	uint16_t cell_index = 0;
	uint16_t cell_count = (uint16_t)geometry->rows * geometry->cols;
	for (ListId block_id = node->data.matrix.cells; block_id != LIST_NULL && cell_index < cell_count;)
	{
		TexListBlock* block = pool_get_list_block(pool, block_id);

		for (uint16_t i = 0; i < block->count && cell_index < cell_count; i++, cell_index++)
		{
			uint8_t r = (uint8_t)(cell_index / geometry->cols);
			uint8_t c = (uint8_t)(cell_index % geometry->cols);
			Node* cell = pool_get_node(pool, block->items[i]);
			if (cell)
			{
				if (cell->w > geometry->col_widths[c])
					geometry->col_widths[c] = cell->w;
				if (cell->asc > geometry->row_ascs[r])
					geometry->row_ascs[r] = cell->asc;
				if (cell->desc > geometry->row_descs[r])
					geometry->row_descs[r] = cell->desc;
			}
		}
		block_id = block->next;
	}

	for (uint8_t c = 0; c < geometry->cols; c++)
		TEX_COORD_ASSIGN(geometry->content_w, geometry->content_w + geometry->col_widths[c]);

	if (geometry->cols > 1)
		TEX_COORD_ASSIGN(geometry->content_w, geometry->content_w + (geometry->cols - 1) * TEX_MATRIX_COL_SPACING);

	for (uint8_t sep_mask = node->data.matrix.col_separators; sep_mask; sep_mask >>= 1)
	{
		if (sep_mask & 1)
			TEX_COORD_ASSIGN(geometry->content_w, geometry->content_w + 2 * TEX_MATRIX_SEP_PAD);
	}

	for (uint8_t r = 0; r < geometry->rows; r++)
		TEX_COORD_ASSIGN(geometry->content_h, geometry->content_h + geometry->row_ascs[r] + geometry->row_descs[r]);

	if (geometry->rows > 1)
		TEX_COORD_ASSIGN(geometry->content_h, geometry->content_h + (geometry->rows - 1) * TEX_MATRIX_ROW_SPACING);

	DelimType type = (DelimType)node->data.matrix.delim_type;
	tex_measure_delimiter_geometry(type, true, geometry->content_h, role, &geometry->left);
	tex_measure_delimiter_geometry(type, false, geometry->content_h, role, &geometry->right);

	// Delimiters are centered on the math axis, including their bottom pixel.
	geometry->axis = tex_font_metric(font, TEX_FONT_AXIS);
	TEX_COORD_ASSIGN(geometry->w, geometry->content_w + geometry->left.advance + geometry->right.advance);
	TEX_COORD_ASSIGN(geometry->asc, geometry->content_h / 2 + geometry->axis);
	TEX_COORD_ASSIGN(geometry->desc, TEX_MAX(0, geometry->content_h - geometry->asc));
	if (type != DELIM_NONE)
		TEX_COORD_ASSIGN(geometry->desc, TEX_MAX(geometry->desc, geometry->content_h / 2 - geometry->axis + 1));
}

void tex_measure_script_geometry(UnifiedPool* pool, const Node* node, TexScriptGeometry* geometry)
{
	const Node* base = pool_get_node(pool, node->data.script.base);
	const Node* sub = pool_get_node(pool, node->data.script.sub);
	const Node* sup = pool_get_node(pool, node->data.script.sup);

	int base_width = base->w;
	int sub_width = sub ? sub->w : 0;
	int sup_width = sup ? sup->w : 0;
	int base_asc = base->asc;
	int base_desc = base->desc;
	TeX_Font font = tex_metrics_font((node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN);
	*geometry = (TexScriptGeometry){ 0 };

	bool limits = (node->flags & TEX_FLAG_MATHF_DISPLAY) &&
	    ((base->flags & TEX_FLAG_LIMITS) || (base->type == N_GLYPH &&
	     (base->data.glyph == (uint8_t)TEXFONT_SUMMATION_CHAR || base->data.glyph == (uint8_t)TEXFONT_PRODUCT_CHAR)));

	if (limits)
	{
		TEX_COORD_ASSIGN(geometry->w, TEX_MAX(base_width, TEX_MAX(sub_width, sup_width)));
		TEX_COORD_ASSIGN(geometry->base_x, (geometry->w - base_width) / 2);
		TEX_COORD_ASSIGN(geometry->sub_x, (geometry->w - sub_width) / 2);
		TEX_COORD_ASSIGN(geometry->sup_x, (geometry->w - sup_width) / 2);
		TEX_COORD_ASSIGN(geometry->sup_y, -base_asc - TEX_MAX(tex_font_metric(font, TEX_FONT_UPPER_LIMIT_RISE),
		    tex_font_metric(font, TEX_FONT_UPPER_LIMIT_GAP) + (sup ? sup->desc : 0)));
		TEX_COORD_ASSIGN(geometry->sub_y, base_desc + TEX_MAX(tex_font_metric(font, TEX_FONT_LOWER_LIMIT_DROP),
		    tex_font_metric(font, TEX_FONT_LOWER_LIMIT_GAP) + (sub ? sub->asc : 0)));
	}
	else
	{
		TexGlyph glyph;
		int italic = (base->type == N_GLYPH || base->type == N_MULTIOP) &&
		    tex_metrics_glyph(base, &glyph) ? glyph.italic_correction : 0;
		TEX_COORD_ASSIGN(geometry->sup_x, base_width);
		TEX_COORD_ASSIGN(geometry->sub_x, base_width - italic);

		int up = tex_font_metric(font,
		    (node->flags & TEX_FLAG_CRAMPED) ? TEX_FONT_SUP_CRAMPED_SHIFT : TEX_FONT_SUP_SHIFT);
		up = TEX_MAX(up, base_asc - tex_font_metric(font, TEX_FONT_SUP_DROP_MAX));
		if (sup)
			up = TEX_MAX(up, sup->desc + tex_font_metric(font, TEX_FONT_SUP_BOTTOM_MIN));

		int down = TEX_MAX(tex_font_metric(font, TEX_FONT_SUB_SHIFT),
		    base_desc + tex_font_metric(font, TEX_FONT_SUB_DROP_MIN));
		if (sub)
			down = TEX_MAX(down, sub->asc - tex_font_metric(font, TEX_FONT_SUB_TOP_MAX));

		if (sub && sup)
		{
			up = TEX_MAX(up, sup->desc + tex_font_metric(font, TEX_FONT_SUP_BOTTOM_WITH_SUB));
			down = TEX_MAX(down, tex_font_metric(font, TEX_FONT_SUB_SUP_GAP) + sup->desc + sub->asc - up);
		}

		TEX_COORD_ASSIGN(geometry->sup_y, -up);
		TEX_COORD_ASSIGN(geometry->sub_y, down);

		int width = base_width;
		if (sup)
			width = TEX_MAX(width, geometry->sup_x + sup_width);
		if (sub)
			width = TEX_MAX(width, geometry->sub_x + sub_width);
		TEX_COORD_ASSIGN(geometry->w, width + tex_font_metric(font, TEX_FONT_SPACE_AFTER_SCRIPT));
	}

	TEX_COORD_ASSIGN(geometry->asc, base_asc);
	TEX_COORD_ASSIGN(geometry->desc, base_desc);
	if (sup)
	{
		TEX_COORD_ASSIGN(geometry->asc, TEX_MAX(geometry->asc, sup->asc - geometry->sup_y));
		TEX_COORD_ASSIGN(geometry->desc, TEX_MAX(geometry->desc, geometry->sup_y + sup->desc));
	}
	if (sub)
	{
		TEX_COORD_ASSIGN(geometry->asc, TEX_MAX(geometry->asc, sub->asc - geometry->sub_y));
		TEX_COORD_ASSIGN(geometry->desc, TEX_MAX(geometry->desc, geometry->sub_y + sub->desc));
	}
}

void tex_measure_frac_geometry(UnifiedPool* pool, const Node* node, TexFracGeometry* geometry)
{
	const Node* num = pool_get_node(pool, node->data.frac.num);
	const Node* den = pool_get_node(pool, node->data.frac.den);
	int num_width = num->w;
	int den_width = den->w;
	TeX_Font font = tex_metrics_font((node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN);
	bool display = (node->flags & TEX_FLAG_MATHF_DISPLAY) != 0;

	geometry->rule = tex_font_metric(font, TEX_FONT_FRAC_RULE);
	TEX_COORD_ASSIGN(geometry->bar_w, TEX_MAX(num_width, den_width));
	geometry->bar_x = 0;
	TEX_COORD_ASSIGN(geometry->bar_y, -tex_font_metric(font, TEX_FONT_AXIS) - geometry->rule / 2);
	geometry->w = geometry->bar_w;
	TEX_COORD_ASSIGN(geometry->num_x, (geometry->w - num_width) / 2);
	TEX_COORD_ASSIGN(geometry->den_x, (geometry->w - den_width) / 2);

	int num_gap = tex_font_metric(font, display ? TEX_FONT_FRAC_NUM_DISPLAY_GAP : TEX_FONT_FRAC_NUM_GAP);
	int den_gap = tex_font_metric(font, display ? TEX_FONT_FRAC_DEN_DISPLAY_GAP : TEX_FONT_FRAC_DEN_GAP);
	TEX_COORD_ASSIGN(geometry->num_y,
	    TEX_MIN(-tex_font_metric(font, display ? TEX_FONT_FRAC_NUM_DISPLAY_SHIFT : TEX_FONT_FRAC_NUM_SHIFT),
	        geometry->bar_y - num_gap - num->desc));
	TEX_COORD_ASSIGN(geometry->den_y,
	    TEX_MAX(tex_font_metric(font, display ? TEX_FONT_FRAC_DEN_DISPLAY_SHIFT : TEX_FONT_FRAC_DEN_SHIFT),
	        geometry->bar_y + geometry->rule + den_gap + den->asc));

	TEX_COORD_ASSIGN(geometry->asc, TEX_MAX(-geometry->bar_y, num->asc - geometry->num_y));
	TEX_COORD_ASSIGN(geometry->desc, TEX_MAX(geometry->bar_y + geometry->rule, geometry->den_y + den->desc));
}

void tex_measure_sqrt_geometry(UnifiedPool* pool, const Node* node, TexSqrtGeometry* geometry)
{
	const Node* rad = pool_get_node(pool, node->data.sqrt.rad);
	const Node* index = pool_get_node(pool, node->data.sqrt.index);
	TeX_Font font = tex_metrics_font((node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN);
	TexGlyph head;
	(void)tex_font_glyph(font, (uint8_t)TEXFONT_SQRT_HEAD_CHAR, &head);

	geometry->rule = tex_font_metric(font, TEX_FONT_RADICAL_RULE);
	geometry->index_x = index ? tex_font_metric(font, TEX_FONT_RADICAL_KERN_BEFORE) : 0;
	TEX_COORD_ASSIGN(geometry->head_x,
	    index ? TEX_MAX(0, geometry->index_x + index->w + tex_font_metric(font, TEX_FONT_RADICAL_KERN_AFTER)) : 0);
	geometry->head_w = head.advance;
	TEX_COORD_ASSIGN(geometry->rad_x, geometry->head_x + geometry->head_w + geometry->rule);
	TEX_COORD_ASSIGN(geometry->w, geometry->rad_x + rad->w);

	int gap = tex_font_metric(font, (node->flags & TEX_FLAG_MATHF_DISPLAY) ? TEX_FONT_RADICAL_DISPLAY_GAP : TEX_FONT_RADICAL_GAP);
	TEX_COORD_ASSIGN(geometry->bar_y, -rad->asc - gap - geometry->rule);
	TEX_COORD_ASSIGN(geometry->bottom_y, rad->desc - 1);

	TEX_COORD_ASSIGN(geometry->index_y,
	    geometry->bottom_y -
	        (geometry->bottom_y - geometry->bar_y) * tex_font_metric(font, TEX_FONT_RADICAL_DEGREE_RAISE) / 100 -
	        (index ? index->desc : 0));
	TEX_COORD_ASSIGN(geometry->asc,
	    TEX_MAX(-geometry->bar_y + tex_font_metric(font, TEX_FONT_RADICAL_EXTRA_ASC),
	        index ? index->asc - geometry->index_y : 0));
	TEX_COORD_ASSIGN(geometry->desc, rad->desc);
}


static void measure_text(UnifiedPool* pool, Node* node, FontRole role)
{
	TexTextMetrics metrics = tex_metrics_text(pool_get_string(pool, node->data.text.sid), node->data.text.len, role);
	node->w = metrics.w;
	node->asc = metrics.asc;
	node->desc = metrics.desc;
}


static void measure_space(Node* node, FontRole role)
{
	int width_mu = node->data.space.mu * tex_font_metric(tex_metrics_font(role), TEX_FONT_EM);
	TEX_COORD_ASSIGN(node->w, (width_mu + (width_mu < 0 ? -9 : 9)) / 18);
	node->asc = 0;
	node->desc = 0;
}

static int accent_height(uint8_t type, FontRole role)
{
	switch (type)
	{
	case ACC_BAR:
		return 1;
	case ACC_DOT:
		return 2;
	case ACC_HAT:
		return 3;
	case ACC_VEC:
		return 4;
	case ACC_DDOT:
		return 2;
	case ACC_TILDE:
		{
			TexGlyph glyph;
			return tex_font_glyph(tex_metrics_font(role), '~', &glyph) ? glyph.height : 0;
		}
	case ACC_OVERLINE:
		/* falls through */
	case ACC_UNDERLINE:
		return 1;
	default:
		TEX_ASSERT(0 && "Invalid AccentType in accent_height");
		return 0;
	}
}

static void measure_multiop(Node* node)
{
	TexGlyph glyph;
	(void)tex_metrics_glyph(node, &glyph);
	int count = node->data.multiop.count;
	TEX_COORD_ASSIGN(node->w, count * glyph.advance + glyph.italic_correction);
	TEX_COORD_ASSIGN(node->asc, TEX_MAX(0, -glyph.y_offset));
	TEX_COORD_ASSIGN(node->desc, TEX_MAX(0, glyph.y_offset + glyph.height));
}

static void aggregate_list(UnifiedPool* pool, ListId head, FontRole role, int* out_w, int* out_asc, int* out_desc)
{
	TexMathCursor cursor;
	tex_math_begin(&cursor, head, role);
	int asc = 0;
	int desc = 0;
	int x;
	Node* node;
	while ((node = tex_math_next(pool, &cursor, &x)))
	{
		asc = TEX_MAX(asc, node->asc);
		desc = TEX_MAX(desc, node->desc);
	}

	*out_w = tex_math_position(&cursor);
	*out_asc = asc;
	*out_desc = desc;
}

static void measure_node(UnifiedPool* pool, Node* node)
{
	FontRole role = (node->flags & TEX_FLAG_SCRIPT) ? FONTROLE_SCRIPT : FONTROLE_MAIN;

	switch (node->type)
	{
	case N_TEXT:
		measure_text(pool, node, role);
		break;
	case N_GLYPH:
		tex_metrics_measure_glyph(node);
		break;
	case N_SPACE:
		measure_space(node, role);
		break;
	case N_MATH:
		{
			// aggregate already measured children
			int w = 0;
			int asc = 0;
			int desc = 0;
			aggregate_list(pool, node->data.list.head, role, &w, &asc, &desc);
			TEX_COORD_ASSIGN(node->w, w);
			TEX_COORD_ASSIGN(node->asc, asc);
			TEX_COORD_ASSIGN(node->desc, desc);
		}
		break;
	case N_SCRIPT:
		{
			TexScriptGeometry geometry;
			tex_measure_script_geometry(pool, node, &geometry);
			node->w = geometry.w;
			node->asc = geometry.asc;
			node->desc = geometry.desc;
		}
		break;
	case N_FRAC:
		{
			TexFracGeometry geometry;
			tex_measure_frac_geometry(pool, node, &geometry);
			node->w = geometry.w;
			node->asc = geometry.asc;
			node->desc = geometry.desc;
		}
		break;
	case N_SQRT:
		{
			TexSqrtGeometry geometry;
			tex_measure_sqrt_geometry(pool, node, &geometry);
			node->w = geometry.w;
			node->asc = geometry.asc;
			node->desc = geometry.desc;
		}
		break;
	case N_OVERLAY:
		{
			Node* base = pool_get_node(pool, node->data.overlay.base);
			int ah = accent_height(node->data.overlay.type, role);
			TEX_COORD_ASSIGN(node->w, base->w);
			int extra = TEX_ACCENT_GAP + ah;
			if (node->data.overlay.type == ACC_UNDERLINE)
			{
				TEX_COORD_ASSIGN(node->asc, base->asc);
				TEX_COORD_ASSIGN(node->desc, base->desc + extra);
			}
			else
			{
				TEX_COORD_ASSIGN(node->asc, base->asc + extra);
				TEX_COORD_ASSIGN(node->desc, base->desc);
			}
		}
		break;
	case N_SPANDECO:
		{
			Node* content = pool_get_node(pool, node->data.spandeco.content);
			Node* label = pool_get_node(pool, node->data.spandeco.label);

			int bh = TEX_BRACE_HEIGHT;
			int gap = TEX_ACCENT_GAP;

			int w = content->w;
			if (label && label->w > w)
				w = label->w;

			TEX_COORD_ASSIGN(node->w, w);

			if (node->data.spandeco.deco_type == DECO_OVERBRACE)
			{
				int label_h = label ? (label->asc + label->desc + gap) : 0;
				TEX_COORD_ASSIGN(node->asc, content->asc + gap + bh + label_h);
				TEX_COORD_ASSIGN(node->desc, content->desc);
			}
			else
			{
				int label_h = label ? (label->asc + label->desc + gap) : 0;
				int ub_gap = gap + 2; // extra space above underbrace for tall delimiters
				TEX_COORD_ASSIGN(node->asc, content->asc);
				TEX_COORD_ASSIGN(node->desc, content->desc + ub_gap + bh + label_h);
			}
		}
		break;
	case N_MULTIOP:
		measure_multiop(node);
		break;
	case N_AUTO_DELIM:
		{
			int content_width = 0;
			int content_asc = 0;
			int content_desc = 0;
			NodeRef content_ref = node->data.auto_delim.content;
			if (content_ref != NODE_NULL)
			{
				aggregate_list(pool, content_ref, role, &content_width, &content_asc, &content_desc);
			}
			else
			{
				content_asc = tex_metrics_asc(role);
				content_desc = tex_metrics_desc(role);
			}

			int axis = tex_font_metric(tex_metrics_font(role), TEX_FONT_AXIS);
			int dist_up = content_asc - axis;
			int dist_down = content_desc + axis;
			int max_dist = TEX_MAX(dist_up, dist_down);

			int min_dist = (tex_metrics_asc(role) + tex_metrics_desc(role)) / 2;
			max_dist = TEX_MAX(max_dist, min_dist);

			int h = max_dist * 2;
			node->data.auto_delim.delim_h = (int16_t)h;

			TexDelimiterGeometry left;
			TexDelimiterGeometry right;
			tex_measure_delimiter_geometry((DelimType)node->data.auto_delim.left_type, true, h, role, &left);
			tex_measure_delimiter_geometry((DelimType)node->data.auto_delim.right_type, false, h, role, &right);
			TEX_COORD_ASSIGN(node->w, left.advance + content_width + right.advance);
			TEX_COORD_ASSIGN(node->asc, axis + (h / 2));
			TEX_COORD_ASSIGN(node->desc, (h / 2) - axis + (left.advance != 0 || right.advance != 0));
		}
		break;
	case N_MATRIX:
		{
			TexMatrixGeometry geometry;
			tex_measure_matrix_geometry(pool, node, &geometry);
			node->w = geometry.w;
			node->asc = geometry.asc;
			node->desc = geometry.desc;
		}
		break;
	default:
		TEX_ASSERT(0 && "Unknown NodeType in measure_node");
		break;
	}
}

void tex_measure_range(UnifiedPool* pool, NodeRef start, NodeRef end)
{
	for (NodeRef i = start; i < end; i++)
	{
		measure_node(pool, pool_get_node(pool, i));
	}
}
