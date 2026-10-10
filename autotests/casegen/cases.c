#include "cases.h"

static const TestCase g_basic_cases[] = {
	{ "fraction_ab", NULL, "$\\frac{a}{b}$", "F62D55BB", 10, 5 },
	{ "sqrt_x", NULL, "$\\sqrt{x}$", "87A1B65D", 10, 5 },
	{ "subscript_n", NULL, "$x_n$", "108E5E56", 10, 5 },
	{ "superscript_2", NULL, "$x^2$", "FF3CC342", 10, 5 },
	{ "sub_sup", NULL, "$x_i^2$", "08721060", 10, 5 },
	{ "fraction_poly", NULL, "$\\frac{x^2 + 1}{y_n}$", "92185D8B", 10, 5 }
};

static const TestCase g_complex_cases[] = {
	{ "quadratic", NULL, "$x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$", "D098D917", 10, 5 },
	{ "integral", NULL, "$\\int_{a}^{b} f(x) \\, dx$", "061F637A", 10, 5 },
	{ "sum_infty", NULL, "$\\sum_{n=0}^{\\infty} \\frac{x^n}{n!}$", "44FE0E16", 10, 5 },
	{ "limit", NULL, "$\\lim_{h \\to 0} \\frac{f(x+h) - f(x)}{h}$", "12047ACF", 10, 5 },
	{ "integral_id", NULL, "$\\int_{a}^{b} f(x) \\, dx = F(b) - F(a)$", "58195CF0", 10, 5 },
	{ "taylor_series", NULL, "$f(x) \\approx f(a) + f'(a)(x-a) + \\frac{f''(a)}{2}(x-a)^2$", "1A1A5D23", 10, 5 },
	{ "maclaurin_exp", NULL, "$e^x = \\sum_{n=0}^{\\infty} \\frac{x^n}{n!}$", "CF7CFEB7", 10, 5 },
	{ "maclaurin_sin", NULL, "$\\sin x = \\sum_{n=0}^{\\infty} \\frac{(-1)^n x^{2n+1}}{(2n+1)!}$", "D7C95749", 10, 5 },
	{ "normal_dist", NULL, "$P(x) = \\frac{1}{\\sigma \\sqrt{2\\pi}} e^{-\\frac{1}{2}\\left(\\frac{x-\\mu}{\\sigma}\\right)^2}$", "260A4247", 10, 5 },
	{ "binom_coeff", NULL, "$\\binom{n}{k} = \\frac{n!}{k!(n-k)!}$", "DB96E7D8", 10, 5 },
	{
		"display_operators", NULL,
		"$$\\int_a^b f(x)\\,dx=F(b)-F(a)$$\n"
		"$$\\sum_{n=0}^{\\infty}\\frac{x^n}{n!}$$\n"
		"$$\\prod_{i=1}^{n}a_i$$",
		"F3B4ED84", 10, 5
	}
};

static const TestCase g_matrix_cases[] = {
	{ "pmatrix_id", NULL, "$\\begin{pmatrix}1 & 0 \\\\ 0 & 1\\end{pmatrix}$", "2EB0801B", 10, 5 },
	{ "bmatrix_abcd", NULL, "$\\begin{bmatrix}a & b \\\\ c & d\\end{bmatrix}$", "BD347B06", 10, 5 },
	{ "vmatrix_abcd", NULL, "$\\begin{vmatrix}a & b \\\\ c & d\\end{vmatrix}$", "11E5DC8A", 10, 5 },
	{ "bmatrix_sys", NULL, "$\\begin{Bmatrix}x + y = 5 \\\\ 2x - y = 1\\end{Bmatrix}$", "EB754E5B", 10, 5 },
	{ "pmatrix_3x3", NULL, "$\\begin{pmatrix}1 & 0 & 0 \\\\ 0 & 1 & 0 \\\\ 0 & 0 & 1\\end{pmatrix}$", "C6B11980", 10, 5 },
	{ "bmatrix_rot", NULL, "$\\begin{bmatrix}\\cos\\theta & -\\sin\\theta \\\\ \\sin\\theta & \\cos\\theta\\end{bmatrix}$", "4F291D25", 10, 5 },
	{ "vmatrix_3x3", NULL, "$\\begin{vmatrix}a & b & c \\\\ d & e & f \\\\ g & h & i\\end{vmatrix}$", "F548A5AE", 10, 5 },
	{ "array_augment", NULL, "$\\left[\\begin{array}{cc|c}1 & 2 & 3 \\\\ 4 & 5 & 6\\end{array}\\right]$", "5BACEC69", 10, 5 }
};

static const TestCase g_accents_cases[] = {
	{ "bar_x", NULL, "$\\bar{x}$", "D9401F8B", 10, 5 },
	{ "hat_x", NULL, "$\\hat{x}$", "5381FB21", 10, 5 },
	{ "vec_v", NULL, "$\\vec{v}$", "2A2299DC", 10, 5 },
	{ "dot_x", NULL, "$\\dot{x}$", "0D2E0D16", 10, 5 },
	{ "underbrace", NULL, "$\\underbrace{a + b + c}_{\\text{sum}}$", "843F1419", 10, 5 },
	{ "overline_ab", NULL, "$\\overline{AB}$", "8AF761A3", 10, 5 },
	{ "tilde_x", NULL, "$\\tilde{x}$", "88C787A8", 10, 5 },
	{ "ddot_x", NULL, "$\\ddot{x}$", "1CE97BF2", 10, 5 }
};

static const TestCase g_operators_cases[] = {
	{ "prod_limits", NULL, "$\\prod_{i=1}^{n} a_i$", "A3A02A1A", 10, 5 },
	{ "nested_frac", NULL, "$\\frac{1}{1 + \\frac{1}{2 + \\frac{1}{3}}}$", "A2E21598", 10, 5 },
	{ "sqrt_nested", NULL, "$\\sqrt{1 + \\sqrt{2 + \\sqrt{3}}}$", "3E5E5CA0", 10, 5 }
};

const TestSuite g_test_suites[] = {
	{ "basic", g_basic_cases, sizeof(g_basic_cases) / sizeof(g_basic_cases[0]) },
	{ "complex", g_complex_cases, sizeof(g_complex_cases) / sizeof(g_complex_cases[0]) },
	{ "matrix", g_matrix_cases, sizeof(g_matrix_cases) / sizeof(g_matrix_cases[0]) },
	{ "accents", g_accents_cases, sizeof(g_accents_cases) / sizeof(g_accents_cases[0]) },
	{ "operators", g_operators_cases, sizeof(g_operators_cases) / sizeof(g_operators_cases[0]) }
};

const size_t g_test_suite_count = sizeof(g_test_suites) / sizeof(g_test_suites[0]);
