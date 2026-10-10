#ifndef TEX_TEX_MEASURE_H
#define TEX_TEX_MEASURE_H

#include "tex_internal.h"

typedef enum
{
	FONTROLE_MAIN = 0,
	FONTROLE_SCRIPT = 1
} FontRole;

typedef struct
{
	int16_t advance;
	int16_t ink_x;
	int16_t ink_w;
} TexDelimiterGeometry;

void tex_measure_delimiter_geometry(DelimType type, bool left, int height, FontRole role, TexDelimiterGeometry* geometry);

typedef struct
{
	int16_t col_widths[TEX_MATRIX_MAX_DIMS];
	int16_t row_ascs[TEX_MATRIX_MAX_DIMS];
	int16_t row_descs[TEX_MATRIX_MAX_DIMS];
	int16_t content_w;
	int16_t content_h;
	int16_t axis;
	TexDelimiterGeometry left;
	TexDelimiterGeometry right;
	int16_t w;
	int16_t asc;
	int16_t desc;
	uint8_t rows;
	uint8_t cols;
} TexMatrixGeometry;


void tex_measure_matrix_geometry(UnifiedPool* pool, const Node* node, TexMatrixGeometry* geometry);

typedef struct
{
	int16_t w;
	int16_t asc;
	int16_t desc;
	int16_t base_x;
	int16_t sub_x;
	int16_t sup_x;
	int16_t sub_y;
	int16_t sup_y;
} TexScriptGeometry;

typedef struct
{
	int16_t w;
	int16_t asc;
	int16_t desc;
	int16_t num_x;
	int16_t num_y;
	int16_t den_x;
	int16_t den_y;
	int16_t bar_x;
	int16_t bar_y;
	int16_t bar_w;
	int16_t rule;
} TexFracGeometry;

typedef struct
{
	int16_t w;
	int16_t asc;
	int16_t desc;
	int16_t head_x;
	int16_t head_w;
	int16_t rad_x;
	int16_t bar_y;
	int16_t bottom_y;
	int16_t index_x;
	int16_t index_y;
	int16_t rule;
} TexSqrtGeometry;

void tex_measure_script_geometry(UnifiedPool* pool, const Node* node, TexScriptGeometry* geometry);
void tex_measure_frac_geometry(UnifiedPool* pool, const Node* node, TexFracGeometry* geometry);
void tex_measure_sqrt_geometry(UnifiedPool* pool, const Node* node, TexSqrtGeometry* geometry);

typedef enum
{
	TEX_ATOM_ORD,
	TEX_ATOM_OP,
	TEX_ATOM_BIN,
	TEX_ATOM_REL,
	TEX_ATOM_OPEN,
	TEX_ATOM_CLOSE,
	TEX_ATOM_PUNCT,
	TEX_ATOM_INNER,
	TEX_ATOM_NONE
} TexAtom;

typedef struct
{
	ListId block;
	uint16_t item;
	int position_mu;
	int16_t em;
	TexAtom previous;
	bool script;
} TexMathCursor;

void tex_math_begin(TexMathCursor* cursor, ListId head, FontRole role);
Node* tex_math_next(UnifiedPool* pool, TexMathCursor* cursor, int* x);
int tex_math_position(const TexMathCursor* cursor);

void tex_measure_range(UnifiedPool* pool, NodeRef start, NodeRef end);

#endif // TEX_TEX_MEASURE_H
