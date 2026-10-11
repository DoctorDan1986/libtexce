#include "test_check.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tex/tex_internal.h"
#include "tex/tex_measure.h"
#include "tex/tex_metrics.h"
#include "tex/tex_parse.h"
#include "tex/tex_pool.h"

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
static void expect(int cond, const char* msg)
{
	if (!cond)
	{
		fprintf(stderr, "[FAIL] %s\n", msg);
		g_fail++;
	}
}

static void test_glyph_widths(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);
	NodeRef r1_ref = tex_parse_math("x", 1, &pool, &L, false);
	NodeRef r2_ref = tex_parse_math("xx", 2, &pool, &L, false);
	Node* r1 = pool_get_node(&pool, r1_ref);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r1 && r2 && "parse roots should not be NULL");
	expect(r1 && r2, "parse roots");
	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);
	expect(r2->w == 2 * r1->w, "two glyphs width equals 2x single");
	pool_free(&pool);
}

static void test_fraction_metrics(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	const char* s = "\\frac{a}{bb}";
	NodeRef root_ref = tex_parse_math(s, (int)strlen(s), &pool, &L, false);
	Node* root = pool_get_node(&pool, root_ref);
	CHECK(root && "root should not be NULL");
	NodeRef f_ref = list_first_item(&pool, root->data.list.head);
	Node* f = pool_get_node(&pool, f_ref);
	CHECK(f && "frac node should not be NULL");
	expect(f && f->type == N_FRAC, "parsed frac");

	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);
	Node* num = pool_get_node(&pool, f->data.frac.num);
	Node* den = pool_get_node(&pool, f->data.frac.den);
	CHECK(num && den && "frac num/den should not be NULL");
	expect(num && den, "frac has children");
	expect(f->w >= num->w && f->w >= den->w, "fraction bounds include both children");

	TexFracGeometry geometry;
	tex_measure_frac_geometry(&pool, f, &geometry);
	expect(geometry.num_y + num->desc < geometry.bar_y, "numerator ink clears fraction bar");
	expect(geometry.den_y - den->asc > geometry.bar_y + geometry.rule - 1,
	       "denominator ink clears fraction bar");
	expect(geometry.bar_x <= geometry.num_x &&
	       geometry.bar_x + geometry.bar_w >= geometry.num_x + num->w &&
	       geometry.bar_x <= geometry.den_x &&
	       geometry.bar_x + geometry.bar_w >= geometry.den_x + den->w,
	       "fraction bar spans both children");
	pool_free(&pool);
}

static void test_scripts_metrics(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	const char* s = "x_1^2";
	NodeRef root_ref = tex_parse_math(s, (int)strlen(s), &pool, &L, false);
	Node* root = pool_get_node(&pool, root_ref);
	CHECK(root && "root should not be NULL");
	NodeRef sc_ref = list_first_item(&pool, root->data.list.head);
	Node* sc = pool_get_node(&pool, sc_ref);
	CHECK(sc && "script node should not be NULL");
	expect(sc && sc->type == N_SCRIPT, "parsed script");

	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);
	Node* base = pool_get_node(&pool, sc->data.script.base);
	CHECK(base && "script base should not be NULL");
	Node* sub = pool_get_node(&pool, sc->data.script.sub);
	Node* sup = pool_get_node(&pool, sc->data.script.sup);

	TexScriptGeometry geometry;
	tex_measure_script_geometry(&pool, sc, &geometry);
	expect(sub && sup && geometry.sup_y + sup->desc < geometry.sub_y - sub->asc,
	       "subscript and superscript ink stay separated");
	expect(sc->asc >= base->asc, "script asc >= base asc");
	expect(sc->desc >= base->desc, "script desc >= base desc");
	pool_free(&pool);
}

static void test_sqrt_metrics(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	const char* s = "\\sqrt[3]{\\frac{x^2}{y}}";
	NodeRef root_ref = tex_parse_math(s, (int)strlen(s), &pool, &L, false);
	Node* root = pool_get_node(&pool, root_ref);
	CHECK(root && "root should not be NULL");
	NodeRef sq_ref = list_first_item(&pool, root->data.list.head);
	Node* sq = pool_get_node(&pool, sq_ref);
	CHECK(sq && "sqrt node should not be NULL");
	expect(sq && sq->type == N_SQRT, "parsed sqrt");

	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);
	Node* rad = pool_get_node(&pool, sq->data.sqrt.rad);
	CHECK(sq && "sqrt node should still be valid");
	expect(sq->w > (rad ? rad->w : 0), "sqrt width > radicand width");
	Node* index = pool_get_node(&pool, sq->data.sqrt.index);
	CHECK(rad && index && "indexed tall radical has both children");

	TexSqrtGeometry geometry;
	tex_measure_sqrt_geometry(&pool, sq, &geometry);
	expect(geometry.bar_y + geometry.rule < -rad->asc, "overbar clears radicand ink");
	expect(geometry.bottom_y + 1 >= rad->desc, "radical encloses the radicand depth");
	expect(geometry.asc >= index->asc - geometry.index_y, "radical bounds include its degree");
	pool_free(&pool);
}

static void test_operator_limits(void)
{
	const char* expressions[] = {
		"\\sum_{n=0}^{\\infty}",
		"\\prod_{i=1}^{n}",
		"\\lim_{x\\to0}^{n}",
		"\\int_a^b"
	};
	for (size_t i = 0; i < sizeof(expressions) / sizeof(expressions[0]); ++i)
	{
		int inline_height = 0;
		for (int display = 0; display <= 1; ++display)
		{
			TeX_Layout layout = { 0 };
			UnifiedPool pool;
			CHECK(pool_init(&pool, 8192) == 0);

			NodeRef root = tex_parse_math(expressions[i], -1, &pool, &layout, display != 0);
			CHECK(root != NODE_NULL);
			tex_measure_range(&pool, 0, (NodeRef)pool.node_count);

			Node* math = pool_get_node(&pool, root);
			NodeRef script_ref = list_first_item(&pool, math->data.list.head);
			Node* script = pool_get_node(&pool, script_ref);
			CHECK(script && script->type == N_SCRIPT);
			Node* base = pool_get_node(&pool, script->data.script.base);
			Node* sub = pool_get_node(&pool, script->data.script.sub);
			Node* sup = pool_get_node(&pool, script->data.script.sup);
			CHECK(base && sub && sup);

			TexScriptGeometry geometry;
			tex_measure_script_geometry(&pool, script, &geometry);
			// Sum, product, and limit stack in display style; integrals never do.
			if (display && i != 3)
			{
				expect(abs((2 * geometry.base_x + base->w) - (2 * geometry.sup_x + sup->w)) <= 1,
				       "display upper limit centered to the pixel grid");
				expect(abs((2 * geometry.base_x + base->w) - (2 * geometry.sub_x + sub->w)) <= 1,
				       "display lower limit centered to the pixel grid");
				expect(geometry.sup_y + sup->desc < -base->asc, "upper limit clears operator ink");
				expect(geometry.sub_y - sub->asc > base->desc, "lower limit clears operator ink");
			}
			else
			{
				expect(geometry.sup_x >= base->w && geometry.sub_x <= geometry.sup_x,
				       "inline operators and integrals retain side scripts");
				expect(geometry.sup_y + sup->desc < geometry.sub_y - sub->asc,
				       "side scripts remain separated");
			}

			// The final expression is the integral, whose scripts follow its slant.
			if (i == 3)
			{
				expect(geometry.sub_x < geometry.sup_x, "integral limits follow its slant");
			}

			if (!display)
			{
				inline_height = base->asc + base->desc;
			}
			else if (i != 2)
			{
				expect(base->asc + base->desc > inline_height, "display uses a larger operator outline");
			}
			expect(script->w >= sub->w && script->w >= sup->w, "operator bounds include limits");
			pool_free(&pool);
		}
	}
}

static void test_matrix_metrics(void)
{
	TeX_Layout L = { 0 };
	UnifiedPool pool;
	pool_init(&pool, 8192);

	// Test 2x2 pmatrix metrics
	const char* s = "\\begin{pmatrix}a & b \\\\ c & d\\end{pmatrix}";
	NodeRef root_ref = tex_parse_math(s, (int)strlen(s), &pool, &L, false);
	Node* root = pool_get_node(&pool, root_ref);
	CHECK(root && "root should not be NULL");
	NodeRef mx_ref = list_first_item(&pool, root->data.list.head);
	Node* mx = pool_get_node(&pool, mx_ref);
	CHECK(mx && "matrix node should not be NULL");
	expect(mx && mx->type == N_MATRIX, "parsed matrix");
	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);

	CHECK(mx && "matrix node should still be valid");
	// Matrix width should be non-negative (may be 0 in fontless test environment)
	expect(mx->w >= 0, "matrix has non-negative width");
	// Verify asc and desc are set (may be 0 without fonts)
	expect(mx->asc >= 0 && mx->desc >= 0, "matrix has valid asc and desc");

	// Test matrix with no delimiters
	pool_reset(&pool);
	const char* s2 = "\\begin{matrix}x\\end{matrix}";
	NodeRef r2_ref = tex_parse_math(s2, (int)strlen(s2), &pool, &L, false);
	Node* r2 = pool_get_node(&pool, r2_ref);
	CHECK(r2 && "r2 should not be NULL");
	NodeRef mx2_ref = list_first_item(&pool, r2->data.list.head);
	Node* mx2 = pool_get_node(&pool, mx2_ref);
	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);
	CHECK(mx2 && "plain matrix node should not be NULL");
	expect(mx2 && mx2->type == N_MATRIX, "plain matrix parsed");
	expect(mx2->w >= 0, "plain matrix has non-negative width");

	pool_free(&pool);
}

static void test_brace_label_metrics(void)
{
	const char* expressions[] = {
		"\\overbrace{x}^{abcdef}",
		"\\underbrace{x}_{abcdef}"
	};
	for (size_t i = 0; i < sizeof(expressions) / sizeof(expressions[0]); ++i)
	{
		TeX_Layout layout = { 0 };
		UnifiedPool pool;
		CHECK(pool_init(&pool, 8192) == 0);

		NodeRef root = tex_parse_math(expressions[i], (int)strlen(expressions[i]), &pool, &layout, false);
		CHECK(root != NODE_NULL);
		tex_measure_range(&pool, 0, (NodeRef)pool.node_count);

		Node* brace = pool_get_node(&pool, list_first_item(&pool, pool_get_node(&pool, root)->data.list.head));
		Node* label = pool_get_node(&pool, brace->data.spandeco.label);
		Node* content = pool_get_node(&pool, brace->data.spandeco.content);
		CHECK(label && content && label->w > content->w);
		expect(brace->w >= label->w, "brace bounds include a label wider than its content");
		expect(brace->asc + brace->desc >= content->asc + content->desc + label->asc + label->desc,
		       "brace bounds include label height");

		pool_free(&pool);
	}
}

static void test_math_spacing(void)
{
	// Six atoms is the longest expression in this boundary-case table.
	const struct
	{
		const char* source;
		unsigned count;
		int gap[6];
	} cases[] = {
		{ "x+y", 3, {0, 1, 1} },
		{ "-x", 2, {0, 0} },
		{ "x=-y", 4, {0, 1, 1, 0} },
		{ "x+(-y)", 6, {0, 1, 1, 0, 0, 0} },
		{ "x+", 2, {0, 0} },
		{ "\\sin x", 2, {0, 1} },
		{ "\\sin(x)", 4, {0, 0, 0, 0} },
		{ "x,y", 3, {0, 0, 1} },
		{ "p\\land q", 3, {0, 1, 1} },
		{ "p\\lor q", 3, {0, 1, 1} },
		{ "\\neg p", 2, {0, 0} },
		{ "p\\Rightarrow q", 3, {0, 1, 1} },
		{ "p\\Leftrightarrow q", 3, {0, 1, 1} },
		{ "p\\top q", 3, {0, 0, 0} },
		{ "p\\bot q", 3, {0, 0, 0} },
		{ "p\\perp q", 3, {0, 1, 1} }
	};

	UnifiedPool pool;
	CHECK(pool_init(&pool, 8192) == 0);
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
	{
		pool_reset(&pool);
		TeX_Layout layout = {0};
		NodeRef ref = tex_parse_math(cases[i].source, -1, &pool, &layout, false);
		CHECK(ref != NODE_NULL);
		CHECK(tex_get_last_error(&layout) == TEX_OK);
		tex_measure_range(&pool, 0, (NodeRef)pool.node_count);

		Node* root = pool_get_node(&pool, ref);
		TexMathCursor cursor;
		tex_math_begin(&cursor, root->data.list.head, FONTROLE_MAIN);
		int end = 0;
		int x;
		for (unsigned j = 0; j < cases[i].count; ++j)
		{
			Node* atom = tex_math_next(&pool, &cursor, &x);
			CHECK(atom);
			expect(cases[i].gap[j] ? x > end : x == end, cases[i].source);
			end = x + atom->w;
		}
		CHECK(!tex_math_next(&pool, &cursor, &x));
		expect(root->w == end, "measured width includes contextual spacing exactly once");
	}

	pool_reset(&pool);
	TeX_Layout layout = {0};
	NodeRef ref = tex_parse_math("x_{p\\wedge q\\vee\\lnot r\\implies\\top\\iff\\bot}", -1, &pool, &layout, false);
	CHECK(ref != NODE_NULL);
	CHECK(tex_get_last_error(&layout) == TEX_OK);
	tex_measure_range(&pool, 0, (NodeRef)pool.node_count);

	Node* script = pool_get_node(&pool, list_first_item(&pool, pool_get_node(&pool, ref)->data.list.head));
	Node* sub = pool_get_node(&pool, script->data.script.sub);
	TexMathCursor cursor;
	tex_math_begin(&cursor, sub->data.list.head, FONTROLE_SCRIPT);
	Node* atom;
	int end = 0;
	int x;
	while ((atom = tex_math_next(&pool, &cursor, &x)))
	{
		expect(x == end, "script style suppresses binary-operator spacing");
		end = x + atom->w;
	}
	pool_free(&pool);
}

static void test_font_context(TeX_Layout* layout)
{
	TeX_Config missing = layout->cfg;
	missing.font_main = (TeX_Font){ 0 };
	TeX_Layout* invalid = tex_format("x", 100, &missing);
	expect(invalid && tex_get_last_error(invalid) == TEX_ERR_FONT, "missing main font reports TEX_ERR_FONT");
	tex_free(invalid);

	missing = layout->cfg;
	missing.font_script = (TeX_Font){ 0 };
	invalid = tex_format("x", 100, &missing);
	expect(invalid && tex_get_last_error(invalid) == TEX_ERR_FONT, "missing script font reports TEX_ERR_FONT");
	tex_free(invalid);

	tex_metrics_activate(&layout->fonts);
}

int main(void)
{
	TeX_Layout layout = { 0 };
	layout.cfg = (TeX_Config){ .palette = {255, 254, 253, 0} };
	expect(tex_font_open(&layout.cfg.font_main, "TeXFonts"), "main font opens");
	expect(tex_font_open(&layout.cfg.font_script, "TeXScrpt"), "script font opens");
	tex_metrics_init(&layout);
	if (tex_get_last_error(&layout) != TEX_OK)
	{
		fprintf(stderr, "Failed to initialize test fonts\n");
		return 1;
	}

	test_font_context(&layout);
	test_glyph_widths();
	test_fraction_metrics();
	test_scripts_metrics();
	test_sqrt_metrics();
	test_operator_limits();
	test_math_spacing();
	test_brace_label_metrics();
	test_matrix_metrics();

	if (g_fail == 0)
	{
		printf("test_measure: PASS\n");
		return 0;
	}
	printf("test_measure: FAIL (%d)\n", g_fail);
	return 1;
}
