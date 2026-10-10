#include "test_check.h"
#include <stdio.h>
#include <string.h>

#include "tex/tex_internal.h"
#include "tex/tex_metrics.h"
#include "tex/tex_parse.h"
#include "tex/tex_pool.h"

// Helper: get the first NodeRef from a ListId (for N_MATH nodes)
static NodeRef list_first_item(UnifiedPool* pool, ListId list_head)
{
	if (list_head == LIST_NULL)
		return NODE_NULL;
	TexListBlock* block = pool_get_list_block(pool, list_head);
	if (!block || block->count == 0)
		return NODE_NULL;
	return block->items[0];
}

static int g_fail = 0;
static void assert_true_int(int cond, const char* msg)
{
	if (!cond)
	{
		fprintf(stderr, "[FAIL] %s\n", msg);
		g_fail++;
	}
}

static void test_frac(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	char buf[] = "\\frac{a}{b}";
	NodeRef root_ref = tex_parse_math(buf, (int)strlen(buf), &pool, &L, false);
	Node* root = pool_get_node(&pool, root_ref);
	CHECK(root && "root should not be NULL");
	assert_true_int(root && root->type == N_MATH, "root is N_MATH");
	Node* f = pool_get_node(&pool, list_first_item(&pool, root->data.list.head));
	CHECK(f && "frac node should not be NULL");
	assert_true_int(f && f->type == N_FRAC, "frac node present");

	Node* num = pool_get_node(&pool, f->data.frac.num);
	Node* den = pool_get_node(&pool, f->data.frac.den);
	CHECK(num && den && "frac num/den should not be NULL");
	assert_true_int(num && den, "frac has num/den");
	assert_true_int(num->type == N_MATH && list_first_item(&pool, num->data.list.head) != NODE_NULL,
	                "num is group math");
	assert_true_int(den->type == N_MATH && list_first_item(&pool, den->data.list.head) != NODE_NULL,
	                "den is group math");

	Node* num_child = pool_get_node(&pool, list_first_item(&pool, num->data.list.head));
	Node* den_child = pool_get_node(&pool, list_first_item(&pool, den->data.list.head));
	CHECK(num_child && den_child && "num/den children should not be NULL");
	assert_true_int(num_child->type == N_GLYPH && num_child->data.glyph == (unsigned char)'a', "num child 'a'");
	assert_true_int(den_child->type == N_GLYPH && den_child->data.glyph == (unsigned char)'b', "den child 'b'");
	pool_free(&pool);
}

static void test_scripts(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	char buf1[] = "x^2";
	NodeRef r1_ref = tex_parse_math(buf1, (int)strlen(buf1), &pool, &L, false);
	Node* r1 = pool_get_node(&pool, r1_ref);
	Node* n1 = pool_get_node(&pool, list_first_item(&pool, r1->data.list.head));
	CHECK(n1 && "script node should not be NULL");
	assert_true_int(n1 && n1->type == N_SCRIPT, "x^2 -> N_SCRIPT");

	Node* base1 = pool_get_node(&pool, n1->data.script.base);
	CHECK(base1 && "script base should not be NULL");
	Node* sup1 = pool_get_node(&pool, n1->data.script.sup);
	assert_true_int(base1 && sup1, "sup set");
	assert_true_int(base1->data.glyph == (unsigned char)'x', "base 'x'");
	assert_true_int(sup1->type == N_MATH || sup1->type == N_GLYPH, "sup parsed");

	pool_reset(&pool);
	char buf2[] = "x_1^2";
	NodeRef r2_ref = tex_parse_math(buf2, (int)strlen(buf2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	Node* n2 = pool_get_node(&pool, list_first_item(&pool, r2->data.list.head));
	CHECK(n2 && "n2 should not be NULL");
	assert_true_int(n2 && n2->type == N_SCRIPT, "x_1^2 -> N_SCRIPT");
	Node* sub2 = pool_get_node(&pool, n2->data.script.sub);
	Node* sup2 = pool_get_node(&pool, n2->data.script.sup);
	assert_true_int(sub2 && sup2, "sub and sup set");
	pool_free(&pool);
}

static void test_overlays(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	char buf1[] = "\\vec{x}";
	NodeRef r1_ref = tex_parse_math(buf1, (int)strlen(buf1), &pool, &L, false);
	Node* r1 = pool_get_node(&pool, r1_ref);
	CHECK(r1 && "r1 should not be NULL");
	Node* n1 = pool_get_node(&pool, list_first_item(&pool, r1->data.list.head));
	CHECK(n1 && "n1 should not be NULL");
	assert_true_int(n1 && n1->type == N_OVERLAY, "vec overlay");

	Node* base1 = pool_get_node(&pool, n1->data.overlay.base);
	CHECK(base1 && "base1 should not be NULL");
	assert_true_int(base1 && base1->type == N_MATH, "overlay base is group");

	pool_reset(&pool);
	char buf2[] = "\\hat{ab}";
	NodeRef r2_ref = tex_parse_math(buf2, (int)strlen(buf2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	Node* n2 = pool_get_node(&pool, list_first_item(&pool, r2->data.list.head));
	CHECK(n2 && "n2 should not be NULL");
	assert_true_int(n2 && n2->type == N_OVERLAY, "hat overlay");

	Node* base2 = pool_get_node(&pool, n2->data.overlay.base);
	CHECK(base2 && "base2 should not be NULL");
	Node* base2_child = pool_get_node(&pool, list_first_item(&pool, base2->data.list.head));
	CHECK(base2_child && "base2_child should not be NULL");
	assert_true_int(base2 && base2_child, "overlay base has child");
	assert_true_int(base2_child->type == N_GLYPH && base2_child->data.glyph == 'a',
	                "overlay base starts with math glyph a");
	TexListBlock* letters = pool_get_list_block(&pool, base2->data.list.head);
	assert_true_int(letters->count == 2 && letters->items[1] == TEX_RESERVED_BASE + 'b',
	                "overlay base preserves separate reserved math glyphs");
	pool_free(&pool);
}

static void test_lim_and_bigops(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	char buf2[] = "\\sum_{i=0}^n";
	NodeRef r2_ref = tex_parse_math(buf2, (int)strlen(buf2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	Node* n2 = pool_get_node(&pool, list_first_item(&pool, r2->data.list.head));
	CHECK(n2 && "n2 should not be NULL");
	assert_true_int(n2 && n2->type == N_SCRIPT, "sum scripts -> N_SCRIPT");

	Node* base = pool_get_node(&pool, n2->data.script.base);
	Node* sub_node = pool_get_node(&pool, n2->data.script.sub);
	Node* sup_node = pool_get_node(&pool, n2->data.script.sup);
	assert_true_int(base && sub_node && sup_node, "sum with sub and sup");
	pool_free(&pool);
}

static void test_auto_delim(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	// Test basic \left( ... \right)
	char buf1[] = "\\left(x\\right)";
	NodeRef r1_ref = tex_parse_math(buf1, (int)strlen(buf1), &pool, &L, false);
	Node* r1 = pool_get_node(&pool, r1_ref);
	CHECK(r1 && "r1 should not be NULL");
	assert_true_int(r1 && r1->type == N_MATH, "left-right root is N_MATH");
	Node* n1 = pool_get_node(&pool, list_first_item(&pool, r1->data.list.head));
	CHECK(n1 && "n1 should not be NULL");
	assert_true_int(n1 && n1->type == N_AUTO_DELIM, "left-right parses to N_AUTO_DELIM");
	assert_true_int(n1->data.auto_delim.left_type == DELIM_PAREN, "left type is DELIM_PAREN");
	assert_true_int(n1->data.auto_delim.right_type == DELIM_PAREN, "right type is DELIM_PAREN");
	assert_true_int(n1->data.auto_delim.content != NODE_NULL, "auto_delim has content");

	// Test \left. ... \right| (mixed delimiters)
	pool_reset(&pool);
	char buf2[] = "\\left.x\\right|";
	NodeRef r2_ref = tex_parse_math(buf2, (int)strlen(buf2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	Node* n2 = pool_get_node(&pool, list_first_item(&pool, r2->data.list.head));
	CHECK(n2 && "n2 should not be NULL");
	assert_true_int(n2 && n2->type == N_AUTO_DELIM, "dot-vert parses to N_AUTO_DELIM");
	assert_true_int(n2->data.auto_delim.left_type == DELIM_NONE, "left type is DELIM_NONE for dot");
	assert_true_int(n2->data.auto_delim.right_type == DELIM_VERT, "right type is DELIM_VERT");

	// Test \left[ ... \right]
	pool_reset(&pool);
	char buf3[] = "\\left[y\\right]";
	NodeRef r3_ref = tex_parse_math(buf3, (int)strlen(buf3), &pool, &L, false);
	Node* r3 = pool_get_node(&pool, r3_ref);
	CHECK(r3 && "r3 should not be NULL");
	Node* n3 = pool_get_node(&pool, list_first_item(&pool, r3->data.list.head));
	CHECK(n3 && "n3 should not be NULL");
	assert_true_int(n3 && n3->type == N_AUTO_DELIM, "bracket parses to N_AUTO_DELIM");
	assert_true_int(n3->data.auto_delim.left_type == DELIM_BRACKET, "left type is DELIM_BRACKET");
	assert_true_int(n3->data.auto_delim.right_type == DELIM_BRACKET, "right type is DELIM_BRACKET");

	// Test \left\{ ... \right\} (curly braces)
	pool_reset(&pool);
	char buf4[] = "\\left\\{x\\right\\}";
	NodeRef r4_ref = tex_parse_math(buf4, (int)strlen(buf4), &pool, &L, false);
	Node* r4 = pool_get_node(&pool, r4_ref);
	CHECK(r4 && "r4 should not be NULL");
	assert_true_int(r4 && r4->type == N_MATH, "brace left-right root is N_MATH");
	Node* n4 = pool_get_node(&pool, list_first_item(&pool, r4->data.list.head));
	CHECK(n4 && "n4 should not be NULL");
	assert_true_int(n4 && n4->type == N_AUTO_DELIM, "brace parses to N_AUTO_DELIM");
	assert_true_int(n4->data.auto_delim.left_type == DELIM_BRACE, "left type is DELIM_BRACE");
	assert_true_int(n4->data.auto_delim.right_type == DELIM_BRACE, "right type is DELIM_BRACE");

	pool_free(&pool);
}

static void test_matrix(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	// Test basic pmatrix with 2x2 cells
	char buf1[] = "\\begin{pmatrix}a & b \\\\ c & d\\end{pmatrix}";
	NodeRef r1_ref = tex_parse_math(buf1, (int)strlen(buf1), &pool, &L, false);
	Node* r1 = pool_get_node(&pool, r1_ref);
	CHECK(r1 && "r1 should not be NULL");
	assert_true_int(r1 && r1->type == N_MATH, "pmatrix root is N_MATH");
	Node* m1 = pool_get_node(&pool, list_first_item(&pool, r1->data.list.head));
	CHECK(m1 && "m1 should not be NULL");
	assert_true_int(m1 && m1->type == N_MATRIX, "pmatrix parses to N_MATRIX");
	assert_true_int(m1->data.matrix.rows == 2, "pmatrix has 2 rows");
	assert_true_int(m1->data.matrix.cols == 2, "pmatrix has 2 cols");
	assert_true_int(m1->data.matrix.delim_type == DELIM_PAREN, "pmatrix delim_type is DELIM_PAREN");

	// Test bmatrix
	pool_reset(&pool);
	char buf2[] = "\\begin{bmatrix}1 & 0 \\\\ 0 & 1\\end{bmatrix}";
	NodeRef r2_ref = tex_parse_math(buf2, (int)strlen(buf2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	Node* m2 = pool_get_node(&pool, list_first_item(&pool, r2->data.list.head));
	CHECK(m2 && "m2 should not be NULL");
	assert_true_int(m2 && m2->type == N_MATRIX, "bmatrix parses to N_MATRIX");
	assert_true_int(m2->data.matrix.delim_type == DELIM_BRACKET, "bmatrix delim_type is DELIM_BRACKET");

	// Test Bmatrix (curly braces)
	pool_reset(&pool);
	char buf3[] = "\\begin{Bmatrix}x \\\\ y\\end{Bmatrix}";
	NodeRef r3_ref = tex_parse_math(buf3, (int)strlen(buf3), &pool, &L, false);
	Node* r3 = pool_get_node(&pool, r3_ref);
	CHECK(r3 && "r3 should not be NULL");
	Node* m3 = pool_get_node(&pool, list_first_item(&pool, r3->data.list.head));
	CHECK(m3 && "m3 should not be NULL");
	assert_true_int(m3 && m3->type == N_MATRIX, "Bmatrix parses to N_MATRIX");
	assert_true_int(m3->data.matrix.delim_type == DELIM_BRACE, "Bmatrix delim_type is DELIM_BRACE");
	assert_true_int(m3->data.matrix.rows == 2, "Bmatrix column vector has 2 rows");
	assert_true_int(m3->data.matrix.cols == 1, "Bmatrix column vector has 1 col");

	// Test vmatrix
	pool_reset(&pool);
	char buf4[] = "\\begin{vmatrix}a\\end{vmatrix}";
	NodeRef r4_ref = tex_parse_math(buf4, (int)strlen(buf4), &pool, &L, false);
	Node* r4 = pool_get_node(&pool, r4_ref);
	CHECK(r4 && "r4 should not be NULL");
	Node* m4 = pool_get_node(&pool, list_first_item(&pool, r4->data.list.head));
	CHECK(m4 && "m4 should not be NULL");
	assert_true_int(m4 && m4->type == N_MATRIX, "vmatrix parses to N_MATRIX");
	assert_true_int(m4->data.matrix.delim_type == DELIM_VERT, "vmatrix delim_type is DELIM_VERT");

	// Test plain matrix (no delimiters)
	pool_reset(&pool);
	char buf5[] = "\\begin{matrix}1 & 2 & 3\\end{matrix}";
	NodeRef r5_ref = tex_parse_math(buf5, (int)strlen(buf5), &pool, &L, false);
	Node* r5 = pool_get_node(&pool, r5_ref);
	CHECK(r5 && "r5 should not be NULL");
	Node* m5 = pool_get_node(&pool, list_first_item(&pool, r5->data.list.head));
	CHECK(m5 && "m5 should not be NULL");
	assert_true_int(m5 && m5->type == N_MATRIX, "matrix parses to N_MATRIX");
	assert_true_int(m5->data.matrix.delim_type == DELIM_NONE, "matrix delim_type is DELIM_NONE");
	assert_true_int(m5->data.matrix.rows == 1, "single-row matrix has 1 row");
	assert_true_int(m5->data.matrix.cols == 3, "3-element row has 3 cols");

	pool_free(&pool);
}

static void test_matrix_empty_cells(void)
{
	UnifiedPool pool;
	if (pool_init(&pool, 8192) != 0)
	{
		assert_true_int(0, "initialize empty-cell test pool");
		return;
	}

	const struct
	{
		const char* input;
		const char* cells; // '.' denotes an explicit empty cell
		int rows;
		int cols;
	} cases[] = {
		{ "\\begin{matrix}a&&b\\end{matrix}", "a.b", 1, 3 },
		{ "\\begin{matrix}&a&\\end{matrix}", ".a.", 1, 3 },
		{ "\\begin{matrix}&&\\end{matrix}", "...", 1, 3 },
		{ "\\begin{matrix}&a&&b&&\\\\c&&d&&&\\\\&&e&&f&\\end{matrix}",
		  ".a.b..c.d.....e.f.", 3, 6 },
		{ "\\begin{matrix}a&\\\\&b\\\\\\end{matrix}", "a..b", 2, 2 },
		{ "\\begin{matrix}\\end{matrix}", "", 0, 0 }
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		pool_reset(&pool);
		NodeRef ref = tex_parse_math(cases[i].input, -1, &pool, &layout, false);
		Node* root = pool_get_node(&pool, ref);
		assert_true_int(root != NULL && layout.error.code == TEX_OK, cases[i].input);
		if (!root)
		{
			continue;
		}

		Node* matrix = pool_get_node(&pool, list_first_item(&pool, root->data.list.head));
		assert_true_int(matrix && matrix->type == N_MATRIX, "empty-cell matrix node");
		if (!matrix)
		{
			continue;
		}
		assert_true_int(matrix->data.matrix.rows == cases[i].rows &&
		                matrix->data.matrix.cols == cases[i].cols, "empty-cell matrix dimensions");

		size_t cell = 0;
		size_t count = strlen(cases[i].cells);
		for (ListId id = matrix->data.matrix.cells; id != LIST_NULL;)
		{
			TexListBlock* block = pool_get_list_block(&pool, id);
			assert_true_int(block != NULL, "empty-cell matrix list block");
			if (!block)
			{
				break;
			}
			for (uint16_t j = 0; j < block->count; ++j, ++cell)
			{
				NodeRef expected = cell < count && cases[i].cells[cell] != '.'
					? TEX_RESERVED_BASE + (unsigned char)cases[i].cells[cell] : NODE_NULL;
				assert_true_int(cell < count && block->items[j] == expected, "matrix cell retains its position");
			}
			id = block->next;
		}
		assert_true_int(cell == count, "matrix retains trailing empty cells");
	}
	pool_free(&pool);
}

static void test_group_termination(void)
{
	UnifiedPool pool;
	if (pool_init(&pool, 8192) != 0)
	{
		assert_true_int(0, "initialize group-termination test pool");
		return;
	}

	const char* invalid[] = { "{x", "x}y", "}", "{{x}", "\\frac{x}{y" };
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		pool_reset(&pool);
		NodeRef root = tex_parse_math(invalid[i], -1, &pool, &layout, false);
		assert_true_int(root == NODE_NULL && layout.error.code == TEX_ERR_PARSE, invalid[i]);
	}

	const char* valid[] = {
		"{}",
		"{{x}{}}y",
		"{\\left({x}\\right)}",
		"{\\begin{matrix}{}&x\\end{matrix}}"
	};
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		pool_reset(&pool);
		NodeRef root = tex_parse_math(valid[i], -1, &pool, &layout, false);
		assert_true_int(root != NODE_NULL && layout.error.code == TEX_OK, valid[i]);
	}

	char deep[TEX_PARSE_MAX_DEPTH + 2];
	memset(deep, '{', sizeof(deep) - 1);
	deep[sizeof(deep) - 1] = '\0';
	TeX_Layout layout = { 0 };
	pool_reset(&pool);
	NodeRef root = tex_parse_math(deep, -1, &pool, &layout, false);
	assert_true_int(root == NODE_NULL && layout.error.code == TEX_ERR_DEPTH,
	                "unclosed groups preserve the first depth error");
	pool_free(&pool);
}

static void test_math_styles_and_atoms(void)
{
	TeX_Layout layout = { 0 };
	UnifiedPool pool;
	CHECK(pool_init(&pool, 8192) == 0);

	for (int display = 0; display < 2; ++display)
	{
		uint8_t style = display ? TEX_FLAG_MATHF_DISPLAY : 0;
		uint8_t child_style = display ? 0 : TEX_FLAG_SCRIPT;

		// Ordinary ASCII atoms share reserved glyph nodes in either style.
		pool_reset(&pool);
		NodeRef ref = tex_parse_math("ab+x=12", -1, &pool, &layout, display != 0);
		Node* root = pool_get_node(&pool, ref);
		CHECK(root && root->flags == style);
		TexListBlock* atoms = pool_get_list_block(&pool, root->data.list.head);
		CHECK(atoms && atoms->count == 7 && pool.node_count == 1);
		for (int i = 0; i < 7; ++i)
		{
			CHECK(atoms->items[i] == TEX_RESERVED_BASE + (unsigned char)"ab+x=12"[i]);
			CHECK(pool_get_node(&pool, atoms->items[i])->flags == 0);
		}

		// Fractions reduce child styles and propagate cramped denominators.
		pool_reset(&pool);
		ref = tex_parse_math("\\frac{x^{\\sin}}{y^{\\cos}_{\\text{z}}}", -1, &pool, &layout, display != 0);
		root = pool_get_node(&pool, ref);
		CHECK(root);
		Node* frac = pool_get_node(&pool, list_first_item(&pool, root->data.list.head));
		CHECK(frac && frac->type == N_FRAC && frac->flags == style);
		Node* num = pool_get_node(&pool, frac->data.frac.num);
		Node* den = pool_get_node(&pool, frac->data.frac.den);
		CHECK(num && num->flags == child_style);
		CHECK(den && den->flags == (child_style | TEX_FLAG_CRAMPED));
		Node* num_script = pool_get_node(&pool, list_first_item(&pool, num->data.list.head));
		Node* den_script = pool_get_node(&pool, list_first_item(&pool, den->data.list.head));
		CHECK(num_script && num_script->flags == child_style);
		CHECK(den_script && den_script->flags == (child_style | TEX_FLAG_CRAMPED));

		Node* sup = pool_get_node(&pool, num_script->data.script.sup);
		CHECK(sup && sup->flags == TEX_FLAG_SCRIPT);
		Node* op = pool_get_node(&pool, list_first_item(&pool, sup->data.list.head));
		CHECK(op && op->type == N_TEXT && op->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_OPERATOR));
		sup = pool_get_node(&pool, den_script->data.script.sup);
		CHECK(sup && sup->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));
		Node* sub = pool_get_node(&pool, den_script->data.script.sub);
		CHECK(sub && sub->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));
		Node* text = pool_get_node(&pool, list_first_item(&pool, sub->data.list.head));
		CHECK(text && text->type == N_TEXT && text->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));

		// Radicands are cramped; optional degrees use script style.
		pool_reset(&pool);
		ref = tex_parse_math("\\sqrt[\\sin]{x^{\\cos}}", -1, &pool, &layout, display != 0);
		root = pool_get_node(&pool, ref);
		CHECK(root);
		Node* radical = pool_get_node(&pool, list_first_item(&pool, root->data.list.head));
		CHECK(radical && radical->type == N_SQRT && radical->flags == style);
		Node* rad = pool_get_node(&pool, radical->data.sqrt.rad);
		Node* index = pool_get_node(&pool, radical->data.sqrt.index);
		CHECK(rad && rad->flags == (style | TEX_FLAG_CRAMPED));
		CHECK(index && index->flags == TEX_FLAG_SCRIPT);
		Node* script = pool_get_node(&pool, list_first_item(&pool, rad->data.list.head));
		CHECK(script && script->flags == (style | TEX_FLAG_CRAMPED));
		sup = pool_get_node(&pool, script->data.script.sup);
		CHECK(sup && sup->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));

		// Grouped operators retain limits while explicit text stays text.
		pool_reset(&pool);
		ref = tex_parse_math("{\\sum}_{\\sin}^{\\cos}\\lim_{\\text{n}}\\text{sin}", -1, &pool, &layout, display != 0);
		root = pool_get_node(&pool, ref);
		CHECK(root);
		atoms = pool_get_list_block(&pool, root->data.list.head);
		CHECK(atoms && atoms->count == 3);
		script = pool_get_node(&pool, atoms->items[0]);
		CHECK(script && script->type == N_SCRIPT && script->flags == style);
		Node* base = pool_get_node(&pool, script->data.script.base);
		CHECK(base && base->type == N_MATH && base->flags == style);
		op = pool_get_node(&pool, list_first_item(&pool, base->data.list.head));
		CHECK(op && op->type == N_GLYPH && op->flags == style);
		sub = pool_get_node(&pool, script->data.script.sub);
		sup = pool_get_node(&pool, script->data.script.sup);
		CHECK(sub && sub->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));
		CHECK(sup && sup->flags == TEX_FLAG_SCRIPT);

		Node* lim = pool_get_node(&pool, atoms->items[1]);
		CHECK(lim && lim->type == N_SCRIPT && lim->flags == style);
		op = pool_get_node(&pool, lim->data.script.base);
		CHECK(op && (op->flags & (TEX_FLAG_OPERATOR | TEX_FLAG_LIMITS)) == (TEX_FLAG_OPERATOR | TEX_FLAG_LIMITS));
		sub = pool_get_node(&pool, lim->data.script.sub);
		CHECK(sub && sub->flags == (TEX_FLAG_SCRIPT | TEX_FLAG_CRAMPED));
		text = pool_get_node(&pool, atoms->items[2]);
		CHECK(text && text->type == N_TEXT && text->flags == style);

		// Explicit spacing retains its signed mu value in either style.
		pool_reset(&pool);
		ref = tex_parse_math("\\,\\:\\;\\!\\quad\\qquad", -1, &pool, &layout, display != 0);
		root = pool_get_node(&pool, ref);
		CHECK(root);
		atoms = pool_get_list_block(&pool, root->data.list.head);
		CHECK(atoms && atoms->count == 6);
		const int16_t mu[] = { 3, 4, 5, -3, 18, 36 };
		for (int i = 0; i < 6; ++i)
		{
			Node* space = pool_get_node(&pool, atoms->items[i]);
			CHECK(space && space->type == N_SPACE && space->flags == style);
			CHECK(space->data.space.mu == mu[i]);
		}

		// Matrix cells reset to inline style without losing script atoms.
		pool_reset(&pool);
		ref = tex_parse_math("\\begin{matrix}ab^2&\\sum\\end{matrix}", -1, &pool, &layout, display != 0);
		root = pool_get_node(&pool, ref);
		CHECK(root);
		Node* matrix = pool_get_node(&pool, list_first_item(&pool, root->data.list.head));
		CHECK(matrix && matrix->type == N_MATRIX && matrix->flags == style);
		atoms = pool_get_list_block(&pool, matrix->data.matrix.cells);
		CHECK(atoms && atoms->count == 2);
		Node* cell = pool_get_node(&pool, atoms->items[0]);
		CHECK(cell && cell->type == N_MATH && cell->flags == 0);
		TexListBlock* letters = pool_get_list_block(&pool, cell->data.list.head);
		CHECK(letters && letters->count == 2 && letters->items[0] == TEX_RESERVED_BASE + 'a');
		script = pool_get_node(&pool, letters->items[1]);
		CHECK(script && script->type == N_SCRIPT && script->data.script.base == TEX_RESERVED_BASE + 'b');
		CHECK(script->data.script.sup == TEX_RESERVED_BASE + 128 + '2');
		op = pool_get_node(&pool, atoms->items[1]);
		CHECK(op && op->type == N_GLYPH && op->flags == 0);
		CHECK(layout.error.code == TEX_OK);
	}
	pool_free(&pool);
}

static void test_required_atoms(void)
{
	UnifiedPool pool;
	if (pool_init(&pool, 8192) != 0)
	{
		assert_true_int(0, "initialize required-atom test pool");
		return;
	}

	const char* invalid[] = {
		"x^",
		"x_",
		"x^\\text",
		"x^{\\text}",
		"\\frac",
		"\\frac{x}",
		"\\binom{n}",
		"\\hat",
		"\\sqrt",
		"\\overbrace",
		"\\overbrace{x}^",
		"\\underbrace{x}_",
		"\\sqrt[\\frac]{x}",
		"\\begin{matrix}x^\\text&y\\end{matrix}"
	};
	for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		pool_reset(&pool);
		NodeRef root = tex_parse_math(invalid[i], -1, &pool, &layout, false);
		assert_true_int(root == NODE_NULL && layout.error.code == TEX_ERR_PARSE, invalid[i]);
	}

	// Explicit empty groups and omitted optional arguments are not missing atoms.
	const char* valid[] = {
		"",
		"{}",
		"x^{}",
		"\\frac{}{x}",
		"\\sqrt{x}",
		"\\sqrt[]{x}",
		"\\overbrace{x}"
	};
	for (size_t i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		pool_reset(&pool);
		NodeRef root = tex_parse_math(valid[i], -1, &pool, &layout, false);
		assert_true_int(root != NODE_NULL && layout.error.code == TEX_OK, valid[i]);
	}

	TeX_Layout layout = { 0 };
	assert_true_int(tex_parse_math("x", -1, NULL, &layout, false) == NODE_NULL &&
	                layout.error.code == TEX_ERR_INPUT, "missing pool reports an input error");
	pool_free(&pool);

	// The first allocation cannot fit; required-child parsing must preserve OOM.
	if (pool_init(&pool, sizeof(Node) - 1) != 0)
	{
		assert_true_int(0, "initialize exhausted test pool");
		return;
	}
	layout = (TeX_Layout){ 0 };
	assert_true_int(tex_parse_math("\\frac{\\alpha}{x}", -1, &pool, &layout, false) == NODE_NULL &&
	                layout.error.code == TEX_ERR_OOM, "allocation failure is not replaced by a syntax error");
	pool_free(&pool);
}

int main(void)
{
	TeX_Layout layout = { 0 };
	CHECK(tex_font_open(&layout.cfg.font_main, "TeXFonts"));
	CHECK(tex_font_open(&layout.cfg.font_script, "TeXScrpt"));
	tex_metrics_init(&layout);
	if (tex_get_last_error(&layout) != TEX_OK)
	{
		fprintf(stderr, "Failed to initialize test fonts\n");
		return 1;
	}

	test_frac();
	test_scripts();
	test_overlays();
	test_lim_and_bigops();
	test_auto_delim();
	test_matrix();
	test_matrix_empty_cells();
	test_group_termination();
	test_math_styles_and_atoms();
	test_required_atoms();

	if (g_fail == 0)
	{
		printf("test_parse: PASS\n");
		return 0;
	}
	printf("test_parse: FAIL (%d)\n", g_fail);
	return 1;
}
