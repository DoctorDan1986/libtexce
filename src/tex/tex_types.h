#ifndef TEX_TEX_TYPES_H
#define TEX_TEX_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "tex_fonts.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
	int v;
} TexCoord;
typedef struct
{
	int v;
} TexBaseline;

// ================================
// error codes
// ================================
typedef enum
{
	TEX_OK = 0,
	TEX_ERR_OOM,
	TEX_ERR_FONT,
	TEX_ERR_PARSE,
	TEX_ERR_INPUT,
	TEX_ERR_DEPTH
} TeX_Error;

// ================================
// error callback
// ================================
// level: 0=info, 1=warning, 2=error
// msg: static string describing the error (may be NULL in release builds)
// file/line: source location (NULL/0 in release builds)
typedef void (*TeX_ErrorLogFn)(void* userdata, int level, const char* msg, const char* file, int line);

// ================================
// configuration
// ================================
typedef struct
{
	// Background, 1/3 ink, 2/3 ink, foreground palette indices, all distinct.
	// Drawing owns the two intermediate entries; the caller supplies endpoint colors.
	uint8_t palette[4];

	bool justify;

	// Validated views; the underlying font data must stay unchanged and stationary.
	TeX_Font font_main;
	TeX_Font font_script;

	TeX_ErrorLogFn error_callback;
	void* error_userdata;
} TeX_Config;

#ifdef __cplusplus
}
#endif

#endif // TEX_TEX_TYPES_H
