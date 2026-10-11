#include "test_check.h"
#include <stdio.h>
#include <string.h>

#include "include/texfont.h"
#include "src/tex/tex_symbols.h"

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static void check_sym(const char* name, SymbolKind kind, int code)
{
	SymbolDesc d;
	memset(&d, 0, sizeof(d));
	int ok = texsym_find(name, strlen(name), &d);
	if (!(ok && d.kind == kind))
	{
		fprintf(stderr, "lookup failed: %s (ok=%d kind=%d)\n", name, ok, d.kind);
		CHECK(0);
	}

	if (kind == SYM_GLYPH)
	{
		int expected_code = (unsigned char)code;
		if ((int)d.code != expected_code)
		{
			fprintf(stderr, "code mismatch: %s -> got=%d expect=%d\n", name, (int)d.code, expected_code);
		}
		CHECK((int)d.code == expected_code);
	}
}

int main(void)
{
	// Glyphs: sample from each category
	check_sym("alpha", SYM_GLYPH, TEXFONT_alpha_CHAR);
	check_sym("Gamma", SYM_GLYPH, TEXFONT_GAMMA_CHAR);
	check_sym("partial", SYM_GLYPH, TEXFONT_PARTIAL_CHAR);
	check_sym("prime", SYM_GLYPH, TEXFONT_PRIME_CHAR);
	check_sym("int", SYM_GLYPH, TEXFONT_INTEGRAL_CHAR);
	check_sym("sum", SYM_GLYPH, TEXFONT_SUMMATION_CHAR);
	check_sym("prod", SYM_GLYPH, TEXFONT_PRODUCT_CHAR);
	check_sym("pm", SYM_GLYPH, TEXFONT_PLUS_MINUS_CHAR);
	check_sym("ge", SYM_GLYPH, TEXFONT_GREATER_EQUAL_CHAR);
	check_sym("to", SYM_GLYPH, TEXFONT_ARROW_RIGHT_CHAR);
	check_sym("gets", SYM_GLYPH, TEXFONT_ARROW_LEFT_CHAR);
	check_sym("langle", SYM_GLYPH, TEXFONT_LANGLE_CHAR);
	check_sym("rangle", SYM_GLYPH, TEXFONT_RANGLE_CHAR);
	check_sym("supset", SYM_GLYPH, TEXFONT_SUPSET_CHAR);
	check_sym("uparrow", SYM_GLYPH, TEXFONT_ARROW_UP_CHAR);
	check_sym("downarrow", SYM_GLYPH, TEXFONT_ARROW_DOWN_CHAR);
	check_sym("odot", SYM_GLYPH, TEXFONT_O_DOT_CHAR);

	// Structural
	SymbolDesc d;
	memset(&d, 0, sizeof(d));
	CHECK(texsym_find("frac", 4, &d) && d.kind == SYM_STRUCT);
	CHECK(texsym_find("sqrt", 4, &d) && d.kind == SYM_STRUCT);

	// Functions
	CHECK(texsym_find("sin", 3, &d) && d.kind == SYM_FUNC);
	CHECK(texsym_find("cos", 3, &d) && d.kind == SYM_FUNC);
	CHECK(texsym_find("tan", 3, &d) && d.kind == SYM_FUNC);
	CHECK(texsym_find("ln", 2, &d) && d.kind == SYM_FUNC);
	CHECK(texsym_find("lim", 3, &d) && d.kind == SYM_FUNC);

	// Accents
	CHECK(texsym_find("vec", 3, &d) && d.kind == SYM_ACCENT);
	CHECK(texsym_find("hat", 3, &d) && d.kind == SYM_ACCENT);
	CHECK(texsym_find("bar", 3, &d) && d.kind == SYM_ACCENT);
	CHECK(texsym_find("dot", 3, &d) && d.kind == SYM_ACCENT);

	// Spaces
	CHECK(texsym_find(",", 1, &d) && d.kind == SYM_SPACE); // thin space
	CHECK(texsym_find(":", 1, &d) && d.kind == SYM_SPACE); // med space
	CHECK(texsym_find(";", 1, &d) && d.kind == SYM_SPACE); // thick space
	CHECK(texsym_find("!", 1, &d) && d.kind == SYM_SPACE); // negative thin
	CHECK(texsym_find("quad", (int)strlen("quad"), &d) && d.kind == SYM_SPACE); // em space

	int ok_qquad = texsym_find("qquad", (int)strlen("qquad"), &d);
	if (!(ok_qquad && d.kind == SYM_SPACE))
	{
		fprintf(stderr, "qquad lookup: ok=%d kind=%d\n", ok_qquad, d.kind);
	}
	CHECK(ok_qquad && d.kind == SYM_SPACE); // 2em space

	// Unknown
	CHECK(!texsym_find("does_not_exist", 14, &d));

	printf("test_symbols: PASS\n");
	return 0;
}
