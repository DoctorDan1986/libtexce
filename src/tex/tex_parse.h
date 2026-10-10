
#ifndef TEX_TEX_PARSE_H
#define TEX_TEX_PARSE_H

#include "tex_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

// Parse a math expression.
// pool: UnifiedPool for node and string allocation
// layout: required error state. NULL is rejected without parsing.
// display: select display style. descendants derive their TeX size/cramped style.
// With a layout, every failure sets its error and returns NODE_NULL.
// Success returns an N_MATH root, including for an empty expression.
// Required child nodes are present on success, optional arguments and empty
// matrix cells may be NODE_NULL, while empty groups have an N_MATH node.
NodeRef tex_parse_math(const char* input, int len, UnifiedPool* pool, TeX_Layout* layout, bool display);

#ifdef __cplusplus
}
#endif

#endif // TEX_TEX_PARSE_H
