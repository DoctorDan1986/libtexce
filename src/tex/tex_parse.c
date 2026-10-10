#include "tex_parse.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "tex_measure.h"
#include "tex_symbols.h"
#include "tex_util.h"

// ------------------
// helpers
// ------------------

typedef enum
{
	M_EOF = 0,
	M_CHAR,
	M_CMD,
	M_LBRACE,
	M_RBRACE,
	M_CARET,
	M_UNDER,
	M_LBRACKET, // '['
	M_RBRACKET, // ']'
	M_AMPERSAND, // '&'
	M_DOUBLE_BACKSLASH // '\\\\'
} MTokenKind;

typedef struct
{
	MTokenKind kind;
	const char* start;
	int len;
} MToken;

typedef struct
{
	const char* cur;
	const char* end;
} MLex;

typedef struct
{
	MLex lx;
	int depth;
	UnifiedPool* pool; // pool for node and string allocation
	TeX_Layout* L; // for error reporting only
	uint8_t style;
} Parser;

typedef struct
{
	ListId head; // first block (LIST_NULL if empty)
	TexListBlock* tail_block; // cached pointer to tail block (for fast append)
} ListBuilder;

static void lb_init(ListBuilder* lb)
{
	lb->head = LIST_NULL;
	lb->tail_block = NULL;
}

static void lb_push(Parser* p, ListBuilder* lb, NodeRef item)
{
	if (TEX_HAS_ERROR(p->L))
		return;

	// NODE_NULL is a positional empty cell in matrix lists
	// Need a new block?
	if (lb->tail_block == NULL || lb->tail_block->count == TEX_LIST_BLOCK_CAP)
	{
		ListId new_id = pool_alloc_list_block(p->pool);
		if (new_id == LIST_NULL)
		{
			TEX_SET_ERROR(p->L, TEX_ERR_OOM, "Failed to allocate list block", 0);
			return;
		}
		TexListBlock* new_block = pool_get_list_block(p->pool, new_id);

		if (lb->head == LIST_NULL)
		{
			lb->head = new_id;
		}
		else
		{
			lb->tail_block->next = new_id;
		}
		lb->tail_block = new_block;
	}

	// Append item to current block
	lb->tail_block->items[lb->tail_block->count++] = item;
}

static NodeRef new_node(Parser* p, NodeType t)
{
	if (TEX_HAS_ERROR(p->L))
		return NODE_NULL;

	NodeRef ref = pool_alloc_node(p->pool);
	if (ref == NODE_NULL)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_OOM, "Failed to allocate parse node", 0);
		return NODE_NULL;
	}
	Node* n = pool_get_node(p->pool, ref);
	n->type = (uint8_t)t;
	n->flags = p->style;
	return ref;
}

static NodeRef make_text(Parser* p, const char* s, size_t len)
{
	NodeRef ref = new_node(p, N_TEXT);
	if (ref == NODE_NULL)
		return NODE_NULL;

	StringId sid = pool_alloc_string(p->pool, s, len);
	if (sid == STRING_NULL)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_OOM, "Failed to allocate text string", 0);
		return NODE_NULL;
	}

	Node* n = pool_get_node(p->pool, ref);
	n->data.text.sid = sid;
	n->data.text.len = (uint16_t)len;
	return ref;
}

static NodeRef make_glyph(Parser* p, uint16_t code)
{
	if (code < 128)
	{
		uint16_t offset = (p->style & TEX_FLAG_SCRIPT) ? 128 : 0;
		return (NodeRef)(TEX_RESERVED_BASE + offset + code);
	}

	// fallback: alloc a new dynamic node for extended characters (Greek, etc)
	NodeRef ref = new_node(p, N_GLYPH);
	if (ref == NODE_NULL)
		return NODE_NULL;

	Node* n = pool_get_node(p->pool, ref);
	n->data.glyph = code;
	return ref;
}
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static NodeRef make_multiop(Parser* p, uint8_t count, uint8_t op_type)
{
	NodeRef ref = new_node(p, N_MULTIOP);
	if (ref == NODE_NULL)
		return NODE_NULL;
	Node* n = pool_get_node(p->pool, ref);
	n->data.multiop.count = count;
	n->data.multiop.op_type = op_type;
	return ref;
}

static void ml_init(MLex* lx, const char* s, int len)
{
	TEX_ASSERT(len >= 0);
	if (len < 0)
		len = 0;
	lx->cur = s;
	lx->end = s + len;
}

static int ml_at_end(MLex* lx) { return lx->cur >= lx->end; }

static MToken ml_next(MLex* lx)
{
	while (!ml_at_end(lx) && isspace((unsigned char)*lx->cur))
		++lx->cur; // ignore ASCII whitespace in math mode
	if (ml_at_end(lx))
	{
		MToken t = { M_EOF, lx->cur, 0 };
		return t;
	}
	char c = *lx->cur;
	if (c == '{')
	{
		++lx->cur;
		MToken t = { M_LBRACE, lx->cur - 1, 1 };
		return t;
	}
	if (c == '}')
	{
		++lx->cur;
		MToken t = { M_RBRACE, lx->cur - 1, 1 };
		return t;
	}
	if (c == '^')
	{
		++lx->cur;
		MToken t = { M_CARET, lx->cur - 1, 1 };
		return t;
	}
	if (c == '_')
	{
		++lx->cur;
		MToken t = { M_UNDER, lx->cur - 1, 1 };
		return t;
	}
	if (c == '[')
	{
		++lx->cur;
		MToken t = { M_LBRACKET, lx->cur - 1, 1 };
		return t;
	}
	if (c == ']')
	{
		++lx->cur;
		MToken t = { M_RBRACKET, lx->cur - 1, 1 };
		return t;
	}
	if (c == '&')
	{
		++lx->cur;
		MToken t = { M_AMPERSAND, lx->cur - 1, 1 };
		return t;
	}
	if (c == '\\')
	{
		const char* cmd_start = lx->cur;
		++lx->cur;

		if (!ml_at_end(lx) && *lx->cur == '\\')
		{
			++lx->cur;
			MToken t = { M_DOUBLE_BACKSLASH, cmd_start, 2 };
			return t;
		}
		const char* s = lx->cur;
		while (!ml_at_end(lx) && isalpha((unsigned char)*lx->cur))
			++lx->cur;
		int len = (int)(lx->cur - s);
		if (len <= 0 && !ml_at_end(lx))
		{
			// single-char command like \^
			s = lx->cur;
			len = 1;
			++lx->cur;
		}
		MToken t = { M_CMD, s, len };
		return t;
	}
	// default: character
	++lx->cur;
	MToken t = { M_CHAR, lx->cur - 1, 1 };
	return t;
}

// ------------------
// recursive descent
// ------------------

static MToken ml_peek(MLex* lx)
{
	MLex tmp = *lx;
	return ml_next(&tmp);
}

static NodeRef parse_list_core(Parser* p, int stop_on_right);
static NodeRef parse_math_list(Parser* p);
static NodeRef parse_atom(Parser* p);
static NodeRef parse_command(Parser* p, const char* name, int len);
static NodeRef parse_auto_delim(Parser* p);

static NodeRef wrap_group_list(Parser* p, ListId list_head)
{
	NodeRef ref = new_node(p, N_MATH);
	if (ref == NODE_NULL)
		return NODE_NULL;
	Node* n = pool_get_node(p->pool, ref);
	n->data.list.head = list_head;
	return ref;
}

static NodeRef parse_atom_noscript(Parser* p);

static NodeRef attach_scripts(Parser* p, NodeRef base)
{
	if (TEX_HAS_ERROR(p->L))
		return NODE_NULL;

	NodeRef sub = NODE_NULL;
	NodeRef sup = NODE_NULL;

	int again = 1;
	while (again)
	{
		again = 0;
		MToken k = ml_peek(&p->lx);
		if (k.kind == M_CARET)
		{
			(void)ml_next(&p->lx);
			if (sup == NODE_NULL)
			{
				uint8_t saved_style = p->style;
				p->style = (saved_style & ~TEX_FLAG_MATHF_DISPLAY) | TEX_FLAG_SCRIPT;
				sup = parse_atom_noscript(p);
				p->style = saved_style;
			}
			if (TEX_HAS_ERROR(p->L))
				return NODE_NULL;
			again = 1;
		}
		else if (k.kind == M_UNDER)
		{
			(void)ml_next(&p->lx);
			if (sub == NODE_NULL)
			{
				uint8_t saved_style = p->style;
				p->style = (saved_style & ~TEX_FLAG_MATHF_DISPLAY) | TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED;
				sub = parse_atom_noscript(p);
				p->style = saved_style;
			}
			if (TEX_HAS_ERROR(p->L))
				return NODE_NULL;
			again = 1;
		}
	}

	if (sub != NODE_NULL || sup != NODE_NULL)
	{
		NodeRef ref = new_node(p, N_SCRIPT);
		if (ref == NODE_NULL)
			return NODE_NULL;
		Node* s = pool_get_node(p->pool, ref);
		s->data.script.base = base;
		s->data.script.sub = sub;
		s->data.script.sup = sup;
		return ref;
	}
	return base;
}

static NodeRef parse_group(Parser* p)
{
	(void)ml_next(&p->lx); // consume '{'
	p->depth++;
	if (p->depth > TEX_PARSE_MAX_DEPTH)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_DEPTH, "Maximum nesting depth exceeded in group", p->depth);
		p->depth--;
		return NODE_NULL;
	}

	ListId head = parse_math_list(p);
	if (ml_peek(&p->lx).kind == M_RBRACE)
	{
		(void)ml_next(&p->lx);
	}
	else
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unclosed '{' in math group", 0);
	}

	p->depth--;
	return wrap_group_list(p, head);
}

// parse an optional bracketed argument [ ... ]; returns NODE_NULL if not present
static NodeRef parse_optional_bracket_arg(Parser* p)
{
	if (ml_peek(&p->lx).kind != M_LBRACKET)
		return NODE_NULL;

	(void)ml_next(&p->lx);
	p->depth++;
	if (p->depth > TEX_PARSE_MAX_DEPTH)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_DEPTH, "Maximum nesting depth exceeded in bracket arg", p->depth);
		p->depth--;
		return NODE_NULL;
	}

	ListBuilder lb;
	lb_init(&lb);

	while (!TEX_HAS_ERROR(p->L))
	{
		MToken pk = ml_peek(&p->lx);
		if (pk.kind == M_RBRACKET)
		{
			(void)ml_next(&p->lx);
			break;
		}
		if (pk.kind == M_EOF)
		{
			TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unclosed '[' in optional argument", 0);
			break;
		}

		NodeRef item = parse_atom(p);
		lb_push(p, &lb, item);
	}

	p->depth--;
	if (lb.head == LIST_NULL)
		return NODE_NULL;
	return wrap_group_list(p, lb.head);
}

// parse one atom without absorbing trailing ^/_ scripts
static NodeRef parse_atom_noscript(Parser* p)
{
	if (TEX_HAS_ERROR(p->L))
		return NODE_NULL;

	MToken t = ml_peek(&p->lx);
	if (t.kind == M_LBRACE)
		return parse_group(p);
	if (t.kind == M_CMD)
	{
		(void)ml_next(&p->lx);
		return parse_command(p, t.start, t.len);
	}
	if (t.kind == M_CHAR)
	{
		(void)ml_next(&p->lx);
		return make_glyph(p, (uint8_t)*t.start);
	}
	if (t.kind == M_LBRACKET || t.kind == M_RBRACKET)
	{
		(void)ml_next(&p->lx);
		// treat [ and ] as standard ASCII glyphs here
		return make_glyph(p, (t.kind == M_LBRACKET) ? '[' : ']');
	}
	if (t.kind == M_CARET || t.kind == M_UNDER)
	{
		(void)ml_next(&p->lx);
		return make_glyph(p, (t.kind == M_CARET) ? '^' : '_');
	}
	(void)ml_next(&p->lx);
	TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Expected math atom", 0);
	return NODE_NULL;
}

// parse one matrix cell until delimiter (&, \\) or \end
// returns NODE_NULL for empty cell, single NodeRef for singleitem cell,
// or N_MATH wrapper for multiitem cell (memory optimization)
static NodeRef parse_matrix_cell(Parser* p)
{
	ListBuilder lb;
	lb_init(&lb);
	int item_count = 0;
	NodeRef first_item = NODE_NULL;

	while (!TEX_HAS_ERROR(p->L))
	{
		MToken pk = ml_peek(&p->lx);
		if (pk.kind == M_AMPERSAND || pk.kind == M_DOUBLE_BACKSLASH || pk.kind == M_EOF || pk.kind == M_RBRACE)
			break;
		if (pk.kind == M_CMD && pk.len == 3 && strncmp(pk.start, "end", 3) == 0)
			break;

		NodeRef item = parse_atom(p);
		if (TEX_HAS_ERROR(p->L))
			break;
		lb_push(p, &lb, item);
		if (item_count == 0)
			first_item = item;
		item_count++;
	}

	if (item_count == 0)
		return NODE_NULL;
	if (item_count == 1)
		return first_item;
	return wrap_group_list(p, lb.head);
}

// parse matrix body until \end, returns N_MATRIX node
static NodeRef parse_matrix_env(Parser* p, DelimType delim_type)
{
	// IMPORTANT: parse all cells FIRST, then allocate the matrix node
	// this makes sure cells are allocated before the matrix in the node pool,
	// so when tex_measure_range iterates in allocation order, cells are
	// measured before the matrix reads their dimensions

	ListBuilder lb;
	lb_init(&lb);

	int row = 0;
	int col = 0;
	int max_cols = 0;

	uint8_t saved_style = p->style;
	p->style &= ~TEX_FLAG_MATHF_DISPLAY;

	while (!TEX_HAS_ERROR(p->L))
	{
		MToken pk = ml_peek(&p->lx);
		if (pk.kind == M_EOF)
		{
			TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unclosed matrix environment", 0);
			break;
		}

		if (col == 0 && pk.kind == M_CMD && pk.len == 3 && strncmp(pk.start, "end", 3) == 0)
			break;

		NodeRef cell = parse_matrix_cell(p);
		lb_push(p, &lb, cell);
		col++;

		pk = ml_peek(&p->lx);
		if (pk.kind == M_AMPERSAND)
		{
			ml_next(&p->lx); // consume &
			// continue to next cell
		}
		else if (pk.kind == M_DOUBLE_BACKSLASH)
		{
			ml_next(&p->lx); // consume row separator
			if (col > max_cols)
				max_cols = col;
			col = 0;
			row++;
		}
		else
		{
			// end of env or error
			if (col > max_cols)
				max_cols = col;
			row++;
			break;
		}
	}
	p->style = saved_style;

	// NOW allocate the matrix node (after all cells have been parsed/allocated)
	NodeRef ref = new_node(p, N_MATRIX);
	if (ref == NODE_NULL)
		return NODE_NULL;

	Node* n = pool_get_node(p->pool, ref);
	n->data.matrix.delim_type = (uint8_t)delim_type;
	n->data.matrix.cells = lb.head;
	n->data.matrix.rows = (uint8_t)row;
	n->data.matrix.cols = (uint8_t)max_cols;

	return ref;
}

// parse \begin{env} ... \end{env}
static NodeRef parse_environment(Parser* p)
{
	// expect opening brace
	MToken t = ml_peek(&p->lx);
	if (t.kind != M_LBRACE)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "expected '{' after \\begin", 0);
		return NODE_NULL;
	}
	ml_next(&p->lx);

	// parse environment name
	const char* name_start = p->lx.cur;
	while (!ml_at_end(&p->lx) && *p->lx.cur != '}')
		++p->lx.cur;
	int name_len = (int)(p->lx.cur - name_start);

	if (ml_at_end(&p->lx))
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unclosed environment name", 0);
		return NODE_NULL;
	}
	++p->lx.cur; // consume '}'

	// map environment name to DelimType
	DelimType delim = DELIM_NONE;
	int is_matrix = 0;
	uint8_t col_separators = 0;

	if (name_len == 6 && strncmp(name_start, "matrix", 6) == 0)
	{
		delim = DELIM_NONE;
		is_matrix = 1;
	}
	else if (name_len == 7 && strncmp(name_start, "pmatrix", 7) == 0)
	{
		delim = DELIM_PAREN;
		is_matrix = 1;
	}
	else if (name_len == 7 && strncmp(name_start, "bmatrix", 7) == 0)
	{
		delim = DELIM_BRACKET;
		is_matrix = 1;
	}
	else if (name_len == 7 && strncmp(name_start, "Bmatrix", 7) == 0)
	{
		delim = DELIM_BRACE;
		is_matrix = 1;
	}
	else if (name_len == 7 && strncmp(name_start, "vmatrix", 7) == 0)
	{
		delim = DELIM_VERT;
		is_matrix = 1;
	}
	else if (name_len == 5 && strncmp(name_start, "array", 5) == 0)
	{
		// array environment: parse column spec {ccc|c}
		delim = DELIM_NONE;
		is_matrix = 1;

		// expect column spec in braces
		MToken spec_brace = ml_peek(&p->lx);
		if (spec_brace.kind == M_LBRACE)
		{
			ml_next(&p->lx); // consume '{'
			int col_count = 0;
			while (!ml_at_end(&p->lx) && *p->lx.cur != '}')
			{
				char c = *p->lx.cur;
				if (c == 'c' || c == 'l' || c == 'r')
				{
					col_count++;
				}
				else if (c == '|')
				{
					// separator after previous column (if any columns exist)
					if (col_count > 0 && col_count <= 8)
					{
						col_separators |= (uint8_t)(1 << (col_count - 1));
					}
				}
				// ignore other characters (spaces, etc)
				++p->lx.cur;
			}
			if (!ml_at_end(&p->lx))
				++p->lx.cur; // consume '}'
		}
	}

	if (!is_matrix)
	{
		// unknown
		return make_text(p, name_start, (size_t)name_len);
	}

	// parse matrix body
	NodeRef matrix = parse_matrix_env(p, delim);

	// set column separators if any (for array environment)
	if (matrix != NODE_NULL && col_separators != 0)
	{
		Node* n = pool_get_node(p->pool, matrix);
		n->data.matrix.col_separators = col_separators;
	}

	// consume \end{...}
	MToken end_tok = ml_peek(&p->lx);
	if (end_tok.kind == M_CMD && end_tok.len == 3 && strncmp(end_tok.start, "end", 3) == 0)
	{
		ml_next(&p->lx); // consume \end

		MToken brace = ml_peek(&p->lx);
		if (brace.kind == M_LBRACE)
		{
			ml_next(&p->lx);

			// skip to closing }
			while (!ml_at_end(&p->lx) && *p->lx.cur != '}')
				++p->lx.cur;
			if (!ml_at_end(&p->lx))
				++p->lx.cur; // consume }
		}
	}

	return matrix;
}


static DelimType parse_delim_type(Parser* p)
{
	MToken t = ml_next(&p->lx);
	if (t.kind == M_CHAR)
	{
		switch (*t.start)
		{
		case '(':
		case ')':
			return DELIM_PAREN;
		case '[':
		case ']':
			return DELIM_BRACKET;
		case '|':
			return DELIM_VERT;
		case '.':
			return DELIM_NONE;
		default:
			break;
		}
	}
	else if (t.kind == M_LBRACKET || t.kind == M_RBRACKET)
	{
		return DELIM_BRACKET;
	}
	else if (t.kind == M_CMD)
	{
		if (t.len == 1 && (*t.start == '{' || *t.start == '}'))
			return DELIM_BRACE;
		if (t.len == 4 && strncmp(t.start, "vert", 4) == 0)
			return DELIM_VERT;
		if (t.len == 5)
		{
			if (strncmp(t.start, "lceil", 5) == 0 || strncmp(t.start, "rceil", 5) == 0)
				return DELIM_CEIL;
		}
		else if (t.len == 6)
		{
			switch (*t.start)
			{
			case 'l':
				if (strncmp(t.start, "lbrace", 6) == 0)
					return DELIM_BRACE;
				if (strncmp(t.start, "langle", 6) == 0)
					return DELIM_ANGLE;
				if (strncmp(t.start, "lfloor", 6) == 0)
					return DELIM_FLOOR;
				break;
			case 'r':
				if (strncmp(t.start, "rbrace", 6) == 0)
					return DELIM_BRACE;
				if (strncmp(t.start, "rangle", 6) == 0)
					return DELIM_ANGLE;
				if (strncmp(t.start, "rfloor", 6) == 0)
					return DELIM_FLOOR;
				break;
			default:
				TEX_ASSERT(0 && "Unexpected first character in 6-char delimiter command");
				break;
			}
		}
	}
	return DELIM_NONE;
}

// parse \left <delim> ... \right <delim> construct
static NodeRef parse_auto_delim(Parser* p)
{
	DelimType l_type = parse_delim_type(p);

	NodeRef content = parse_list_core(p, 1);

	MToken t = ml_peek(&p->lx);
	if (t.kind == M_CMD && t.len == 5 && strncmp(t.start, "right", 5) == 0)
	{
		ml_next(&p->lx);
	}
	else
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unbalanced \\left - missing \\right", 0);
		return NODE_NULL;
	}

	DelimType r_type = parse_delim_type(p);

	NodeRef ref = new_node(p, N_AUTO_DELIM);
	if (ref == NODE_NULL)
		return NODE_NULL;
	Node* n = pool_get_node(p->pool, ref);
	n->data.auto_delim.content = content;
	n->data.auto_delim.left_type = (uint8_t)l_type;
	n->data.auto_delim.right_type = (uint8_t)r_type;
	return ref;
}

static const struct
{
	const char* str;
	int len;
} g_func_text[] = {
	{ NULL, 0 }, // 0 (unused)
	{ "sin", 3 }, // SYMC_FUNC_SIN
	{ "cos", 3 }, // SYMC_FUNC_COS
	{ "tan", 3 }, // SYMC_FUNC_TAN
	{ "ln", 2 }, // SYMC_FUNC_LN
	{ "lim", 3 }, // SYMC_FUNC_LIM
	{ "log", 3 }, // SYMC_FUNC_LOG
	{ "exp", 3 }, // SYMC_FUNC_EXP
	{ "min", 3 }, // SYMC_FUNC_MIN
	{ "max", 3 }, // SYMC_FUNC_MAX
	{ "sup", 3 }, // SYMC_FUNC_SUP
	{ "inf", 3 }, // SYMC_FUNC_INF
	{ "det", 3 }, // SYMC_FUNC_DET
	{ "gcd", 3 }, // SYMC_FUNC_GCD
	{ "deg", 3 }, // SYMC_FUNC_DEG
	{ "dim", 3 }, // SYMC_FUNC_DIM
	{ "sec", 3 }, // SYMC_FUNC_SEC
	{ "csc", 3 }, // SYMC_FUNC_CSC
	{ "cot", 3 }, // SYMC_FUNC_COT
	{ "arcsin", 6 }, // SYMC_FUNC_ARCSIN
	{ "arccos", 6 }, // SYMC_FUNC_ARCCOS
	{ "arctan", 6 }, // SYMC_FUNC_ARCTAN
	{ "sinh", 4 }, // SYMC_FUNC_SINH
	{ "cosh", 4 }, // SYMC_FUNC_COSH
	{ "tanh", 4 }, // SYMC_FUNC_TANH
	{ "arg", 3 }, // SYMC_FUNC_ARG
	{ "ker", 3 }, // SYMC_FUNC_KER
	{ "Pr", 2 }, // SYMC_FUNC_PR
	{ "hom", 3 }, // SYMC_FUNC_HOM
	{ "lg", 2 }, // SYMC_FUNC_LG
	{ "coth", 4 }, // SYMC_FUNC_COTH
};

static NodeRef parse_text_arg(Parser* p)
{
	// expect opening brace
	MToken t = ml_peek(&p->lx);
	if (t.kind != M_LBRACE)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "expected '{' after \\text", 0);
		return NODE_NULL;
	}

	// consume '{'
	ml_next(&p->lx);

	// scan raw content until '}' handling escapes
	const char* start = p->lx.cur;
	const char* cur = start;
	int needs_unescape = 0;

	while (cur < p->lx.end)
	{
		if (*cur == '}')
		{
			break; // found closing brace
		}
		if (*cur == '\\')
		{
			needs_unescape = 1;
			// skip next char (escape sequence)
			if (cur + 1 < p->lx.end)
			{
				cur += 2;
				continue;
			}
		}
		cur++;
	}

	if (cur >= p->lx.end)
	{
		TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unclosed \\text argument", 0);
		return NODE_NULL;
	}

	// length of the segment inside { ... }
	int raw_len = (int)(cur - start);

	// create N_TEXT node
	NodeRef ref = new_node(p, N_TEXT);
	if (ref == NODE_NULL)
		return NODE_NULL;
	Node* n = pool_get_node(p->pool, ref);

	if (needs_unescape)
	{
		// allocate unescaped length directly in pool then overwrite with correct content
		int ulen = tex_util_unescaped_len(start, raw_len);
		StringId sid = pool_alloc_string(p->pool, start, (size_t)ulen);
		if (sid == STRING_NULL)
		{
			TEX_SET_ERROR(p->L, TEX_ERR_OOM, "OOM parsing \\text", 0);
			return NODE_NULL;
		}
		// overwrite dummy copy with unescaped content
		char* buf = (char*)(p->pool->slab + sid);
		tex_util_copy_unescaped(buf, start, raw_len);
		n->data.text.sid = sid;
		n->data.text.len = (uint16_t)ulen;
	}
	else
	{
		// allocate copy in pool (strings must be in pool for serialization)
		StringId sid = pool_alloc_string(p->pool, start, (size_t)raw_len);
		if (sid == STRING_NULL)
		{
			TEX_SET_ERROR(p->L, TEX_ERR_OOM, "OOM parsing \\text", 0);
			return NODE_NULL;
		}
		n->data.text.sid = sid;
		n->data.text.len = (uint16_t)raw_len;
	}

	// manually advance lexer past the processed text and the closing '}'
	p->lx.cur = cur + 1;

	return ref;
}

static NodeRef parse_command(Parser* p, const char* name, int len)
{
	SymbolDesc d;
	int found = texsym_find(name, (size_t)len, &d);

	if (!found)
	{
		return make_text(p, name, (size_t)len);
	}
	switch (d.kind)
	{
	case SYM_GLYPH:
		return make_glyph(p, d.code);
	case SYM_SPACE:
		{
			NodeRef ref = new_node(p, N_SPACE);
			if (ref == NODE_NULL)
				return NODE_NULL;
			Node* space = pool_get_node(p->pool, ref);
			int16_t mu = 0;
			if (d.code == SYMC_THINSPACE)
			{
				mu = 3;
			}
			else if (d.code == SYMC_MEDSPACE)
			{
				mu = 4;
			}
			else if (d.code == SYMC_THICKSPACE)
			{
				mu = 5;
			}
			else if (d.code == SYMC_NEGSPACE)
			{
				mu = -3;
			}
			else if (d.code == SYMC_QUAD)
			{
				mu = 18;
			}
			else if (d.code == SYMC_QQUAD)
			{
				mu = 36;
			}

			space->data.space.mu = mu;
			return ref;
		}
	case SYM_ACCENT:
		{
			NodeRef base = NODE_NULL;
			MToken pk = ml_peek(&p->lx);
			if (pk.kind == M_LBRACE)
				base = parse_group(p);
			else
				base = parse_atom(p);
			NodeRef ref = new_node(p, N_OVERLAY);
			if (ref == NODE_NULL)
				return NODE_NULL;
			Node* ov = pool_get_node(p->pool, ref);
			ov->data.overlay.base = base;
			// Map code -> AccentType
			uint8_t at = 0;
			if (d.code == SYMC_ACC_VEC)
				at = ACC_VEC;
			else if (d.code == SYMC_ACC_HAT)
				at = ACC_HAT;
			else if (d.code == SYMC_ACC_BAR)
				at = ACC_BAR;
			else if (d.code == SYMC_ACC_DOT)
				at = ACC_DOT;
			else if (d.code == SYMC_ACC_DDOT)
				at = ACC_DDOT;
			else if (d.code == SYMC_ACC_OVERLINE)
				at = ACC_OVERLINE;
			else if (d.code == SYMC_ACC_UNDERLINE)
				at = ACC_UNDERLINE;
			else if (d.code == SYMC_ACC_TILDE)
				at = ACC_TILDE;
			ov->data.overlay.type = at;
			return ref;
		}
	case SYM_STRUCT:
		{
			if (d.code == SYMC_BEGIN)
			{
				return parse_environment(p);
			}
			if (d.code == SYMC_END)
			{
				// orphan \end without matching \begin
				TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unexpected \\end without \\begin", 0);
				return NODE_NULL;
			}
			if (d.code == SYMC_TEXT)
			{
				return parse_text_arg(p);
			}
			if (d.code == SYMC_FRAC)
			{
				uint8_t saved_style = p->style;
				p->style = saved_style & ~TEX_FLAG_MATHF_DISPLAY;
				if (!(saved_style & TEX_FLAG_MATHF_DISPLAY))
				{
					p->style |= TEX_FLAG_SCRIPT;
				}

				NodeRef numerator = NODE_NULL;
				NodeRef denominator = NODE_NULL;
				if (ml_peek(&p->lx).kind == M_LBRACE)
				{
					numerator = parse_group(p);
				}
				else
				{
					numerator = parse_atom(p);
				}

				p->style |= TEX_FLAG_CRAMPED;
				if (ml_peek(&p->lx).kind == M_LBRACE)
				{
					denominator = parse_group(p);
				}
				else
				{
					denominator = parse_atom(p);
				}
				p->style = saved_style;

				NodeRef ref = new_node(p, N_FRAC);
				if (ref == NODE_NULL)
					return NODE_NULL;

				Node* fraction = pool_get_node(p->pool, ref);
				fraction->data.frac.num = numerator;
				fraction->data.frac.den = denominator;
				return ref;
			}
			if (d.code == SYMC_BINOM)
			{
				uint8_t saved_style = p->style;
				p->style = saved_style & ~TEX_FLAG_MATHF_DISPLAY;
				if (!(saved_style & TEX_FLAG_MATHF_DISPLAY))
				{
					p->style |= TEX_FLAG_SCRIPT;
				}

				NodeRef numerator = NODE_NULL;
				NodeRef denominator = NODE_NULL;
				if (ml_peek(&p->lx).kind == M_LBRACE)
				{
					numerator = parse_group(p);
				}
				else
				{
					numerator = parse_atom(p);
				}

				p->style |= TEX_FLAG_CRAMPED;
				if (ml_peek(&p->lx).kind == M_LBRACE)
				{
					denominator = parse_group(p);
				}
				else
				{
					denominator = parse_atom(p);
				}
				p->style = saved_style;

				ListBuilder lb;
				lb_init(&lb);
				lb_push(p, &lb, numerator);
				lb_push(p, &lb, denominator);

				NodeRef ref = new_node(p, N_MATRIX);
				if (ref == NODE_NULL)
					return NODE_NULL;
				Node* n = pool_get_node(p->pool, ref);
				n->data.matrix.delim_type = (uint8_t)DELIM_PAREN;
				n->data.matrix.cells = lb.head;
				n->data.matrix.rows = 2;
				n->data.matrix.cols = 1;
				n->data.matrix.col_separators = 0;
				return ref;
			}
			else if (d.code == SYMC_SQRT)
			{
				uint8_t saved_style = p->style;
				p->style = TEX_FLAG_SCRIPT;
				NodeRef index = parse_optional_bracket_arg(p);
				p->style = saved_style;
				if (TEX_HAS_ERROR(p->L))
					return NODE_NULL;

				p->style |= TEX_FLAG_CRAMPED;
				NodeRef radicand = NODE_NULL;
				MToken next_token = ml_peek(&p->lx);
				if (next_token.kind == M_LBRACE)
				{
					radicand = parse_group(p);
				}
				else
				{
					radicand = parse_atom(p);
				}
				p->style = saved_style;
				NodeRef ref = new_node(p, N_SQRT);
				if (ref == NODE_NULL)
					return NODE_NULL;

				Node* radical = pool_get_node(p->pool, ref);
				radical->data.sqrt.rad = radicand;
				radical->data.sqrt.index = index; // NODE_NULL if not provided
				return ref;
			}
			else if (d.code == SYMC_OVERBRACE || d.code == SYMC_UNDERBRACE)
			{
				NodeRef content = NODE_NULL;
				MToken pk = ml_peek(&p->lx);
				if (pk.kind == M_LBRACE)
					content = parse_group(p);
				else
					content = parse_atom(p);

				NodeRef label = NODE_NULL;

				// ^ for overbrace, _ for underbrace, label is script context
				MToken k = ml_peek(&p->lx);
				if ((d.code == SYMC_OVERBRACE && k.kind == M_CARET) || (d.code == SYMC_UNDERBRACE && k.kind == M_UNDER))
				{
					(void)ml_next(&p->lx);
					uint8_t saved_style = p->style;
					p->style = (saved_style & ~TEX_FLAG_MATHF_DISPLAY) | TEX_FLAG_SCRIPT;
					if (d.code == SYMC_UNDERBRACE)
					{
						p->style |= TEX_FLAG_CRAMPED;
					}
					label = parse_atom_noscript(p);
					p->style = saved_style;
				}

				NodeRef ref = new_node(p, N_SPANDECO);
				if (ref == NODE_NULL)
					return NODE_NULL;
				Node* n = pool_get_node(p->pool, ref);
				n->data.spandeco.content = content;
				n->data.spandeco.label = label;
				n->data.spandeco.deco_type = (d.code == SYMC_OVERBRACE) ? DECO_OVERBRACE : DECO_UNDERBRACE;
				return ref;
			}
			break;
		}
	case SYM_FUNC:
		{
			NodeRef ref = make_text(p, g_func_text[d.code].str, (size_t)g_func_text[d.code].len);
			if (ref == NODE_NULL)
				return NODE_NULL;

			Node* n = pool_get_node(p->pool, ref);
			n->flags |= TEX_FLAG_OPERATOR;
			if (d.code == SYMC_FUNC_LIM)
			{
				n->flags |= TEX_FLAG_LIMITS;
			}
			return ref;
		}
	case SYM_MULTIOP:
		{
			uint8_t count = 2; // default for \iint
			uint8_t op_type = MULTIOP_INT;

			if (d.code == SYMC_MULTIINT_2)
				count = 2;
			else if (d.code == SYMC_MULTIINT_3)
				count = 3;
			else if (d.code == SYMC_MULTIINT_4)
				count = 4;
			else if (d.code == SYMC_MULTI_OINT_1)
			{
				count = 1;
				op_type = MULTIOP_OINT;
			}
			else if (d.code == SYMC_MULTI_OINT_2)
			{
				count = 2;
				op_type = MULTIOP_OINT;
			}
			else if (d.code == SYMC_MULTI_OINT_3)
			{
				count = 3;
				op_type = MULTIOP_OINT;
			}

			return make_multiop(p, count, op_type);
		}
	case SYM_DELIM_MOD:
		{
			if (len == 4 && strncmp(name, "left", 4) == 0)
			{
				return parse_auto_delim(p);
			}
			// Orphan \right is an error
			TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Unexpected \\right without \\left", 0);
			return NODE_NULL;
		}
	case SYM_NONE:
		break;
	}
	// fallback to literal text
	return make_text(p, name, (size_t)len);
}

// Callers handle list terminators. A required atom either exists or sets L's error.
static NodeRef parse_atom(Parser* p)
{
	if (TEX_HAS_ERROR(p->L))
		return NODE_NULL;

	MToken t = ml_peek(&p->lx);
	if (t.kind == M_LBRACE)
	{
		return attach_scripts(p, parse_group(p));
	}
	if (t.kind == M_CMD)
	{
		(void)ml_next(&p->lx);
		return attach_scripts(p, parse_command(p, t.start, t.len));
	}
	if (t.kind == M_CHAR)
	{
		(void)ml_next(&p->lx);
		NodeRef n = make_glyph(p, (uint8_t)*t.start);
		return attach_scripts(p, n);
	}
	if (t.kind == M_LBRACKET || t.kind == M_RBRACKET)
	{
		(void)ml_next(&p->lx);
		NodeRef n = make_glyph(p, (t.kind == M_LBRACKET) ? '[' : ']');
		return attach_scripts(p, n);
	}
	if (t.kind == M_CARET || t.kind == M_UNDER)
	{
		(void)ml_next(&p->lx);
		NodeRef n = make_glyph(p, (t.kind == M_CARET) ? '^' : '_');
		return n;
	}

	(void)ml_next(&p->lx);
	TEX_SET_ERROR(p->L, TEX_ERR_PARSE, "Expected math atom", 0);
	return NODE_NULL;
}

static ListId parse_list_core(Parser* p, int stop_on_right)
{
	ListBuilder lb;
	lb_init(&lb);

	while (!TEX_HAS_ERROR(p->L))
	{
		MToken pk = ml_peek(&p->lx);
		if (pk.kind == M_EOF || pk.kind == M_RBRACE)
			break;
		if (stop_on_right && pk.kind == M_CMD && pk.len == 5 && strncmp(pk.start, "right", 5) == 0)
			break;

		NodeRef item = parse_atom(p);
		lb_push(p, &lb, item);
	}

	return lb.head;
}

static ListId parse_math_list(Parser* p)
{
	return parse_list_core(p, 0);
}

NodeRef tex_parse_math(const char* input, int len, UnifiedPool* pool, TeX_Layout* layout, bool display)
{
	if (!layout)
		return NODE_NULL;

	if (!pool || !input)
	{
		TEX_SET_ERROR(layout, TEX_ERR_INPUT, "Math parser requires input and pool", 0);
		return NODE_NULL;
	}
	if (len < 0)
		len = (int)strlen(input);

	Parser p;
	ml_init(&p.lx, input, len);
	p.depth = 0;
	p.pool = pool;
	p.L = layout;
	p.style = display ? TEX_FLAG_MATHF_DISPLAY : 0;

	ListId seq = parse_math_list(&p);
	if (TEX_HAS_ERROR(layout))
	{
		return NODE_NULL;
	}
	if (ml_peek(&p.lx).kind != M_EOF)
	{
		TEX_SET_ERROR(layout, TEX_ERR_PARSE, "Unexpected math list terminator", 0);
		return NODE_NULL;
	}

	NodeRef root = new_node(&p, N_MATH);
	if (root == NODE_NULL)
		return NODE_NULL; // error already set by new_node

	Node* root_node = pool_get_node(pool, root);
	root_node->data.list.head = seq;
	return root;
}
