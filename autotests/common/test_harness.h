/**
 * test_harness.h - Shared test harness for libtexce autotests
 * 
 * Usage:
 *   #include "test_harness.h"
 *   int main(void) {
 *       TEST_INIT();
 *       TEST_RENDER("$\\frac{a}{b}$");
 *       TEST_WAIT_KEY();
 *       TEST_CLEANUP();
 *       return 0;
 *   }
 */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <graphx.h>
#include <ti/getcsc.h>
#include <tice.h>

#include "tex/tex.h"

#define TEST_WIDTH 300

static TeX_Config g_test_config = { .palette = { 255, 254, 253, 0 } };
static TeX_Renderer *g_test_renderer = NULL;

/**
 * Initialize graphics and fonts. Returns 1 on error.
 */
static inline int test_harness_init(void)
{
    gfx_Begin();
    gfx_FillScreen(g_test_config.palette[0]);

    if (!tex_font_open(&g_test_config.font_main, "TeXFonts") ||
        !tex_font_open(&g_test_config.font_script, "TeXScrpt"))
    {
        gfx_SetTextFGColor(g_test_config.palette[3]);
        gfx_PrintStringXY("Font Error", 10, 10);
        while (!os_GetCSC())
            ;
        gfx_End();
        return 1;
    }

    g_test_renderer = tex_renderer_create();
    if (!g_test_renderer)
    {
        gfx_SetTextFGColor(g_test_config.palette[3]);
        gfx_PrintStringXY("Renderer Error", 10, 10);
        while (!os_GetCSC())
            ;
        gfx_End();
        return 1;
    }

    return 0;
}

/**
 * render a LaTeX expression at the given position
 * returns the vertical position for the next expression
 */
static inline int test_harness_render(const char *expr, int x, int y)
{
    TeX_Layout *layout = tex_format(expr, TEST_WIDTH, &g_test_config);
    if (layout)
    {
        tex_draw(g_test_renderer, layout, x, y, 0);
        y += tex_get_total_height(layout) + 5;
        tex_free(layout);
    }

    return y;
}

static inline void test_harness_cleanup(void)
{
    tex_renderer_destroy(g_test_renderer);
    gfx_End();
}

#define TEST_INIT() do { \
    if (test_harness_init()) \
        return 1; \
    gfx_FillScreen(g_test_config.palette[0]); \
} while (0)

#define TEST_RENDER(expr, x, y) test_harness_render((expr), (x), (y))

#define TEST_WAIT_KEY() do { \
    while (!os_GetCSC()) {} \
} while (0)

#define TEST_CLEANUP() test_harness_cleanup()

#endif /* TEST_HARNESS_H */
