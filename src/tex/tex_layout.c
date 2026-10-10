#include <stdlib.h>
#include <string.h>

#if defined(__TICE__)
#include <debug.h>
#endif

#include "tex.h"
#include "tex_internal.h"
#include "tex_measure.h"
#include "tex_metrics.h"
#include "tex_parse.h"
#include "tex_token.h"
#include "tex_renderer.h"
#include "tex_util.h"

#if defined(__TICE__)
#define TEX_LAYOUT_SCRATCH_SIZE ((size_t)4 * 1024)
#else
#define TEX_LAYOUT_SCRATCH_SIZE ((size_t)8 * 1024)
#endif

void tex_flow_init(TeX_Flow* flow, const TeX_Layout* layout, const char* source, int y)
{
	memset(flow, 0, sizeof(*flow));
	flow->stream.cursor = source;
	flow->stream.end = layout->source_end;
	flow->y = y;
}

const char* tex_flow_position(const TeX_Flow* flow)
{
	return flow->has_token ? flow->token_source : flow->stream.cursor;
}

static int flow_read(TeX_Flow* flow, TeX_Layout* layout, UnifiedPool* pool, int retain)
{
	if (flow->has_token)
	{
		return 1;
	}
	if (flow->eof || flow->failed)
	{
		return 0;
	}
	if (!retain)
	{
		pool_reset(pool);
	}

	flow->token_source = flow->stream.cursor;
	if (!tex_stream_next(&flow->stream, &flow->token, pool, layout))
	{
		flow->eof = flow->stream.cursor == flow->stream.end;
		flow->failed = !flow->eof;
		return 0;
	}

	flow->has_token = 1;
	flow->node = NODE_NULL;
	switch (flow->token.type)
	{
	case T_TEXT:
		{
			TexTextMetrics metrics = tex_metrics_text(flow->token.start, flow->token.len, FONTROLE_MAIN);
			flow->w = metrics.w;
			flow->asc = metrics.asc;
			flow->desc = metrics.desc;
		}
		break;
	case T_MATH_INLINE:
	case T_MATH_DISPLAY:
		{
			NodeRef first_node = (NodeRef)pool->node_count;
			flow->node = tex_parse_math(flow->token.start, flow->token.len, pool, layout,
			                           flow->token.type == T_MATH_DISPLAY);
			if (flow->node != NODE_NULL)
			{
				Node* node = pool_get_node(pool, flow->node);
				tex_measure_range(pool, first_node, (NodeRef)pool->node_count);
				flow->w = node->w;
				flow->asc = node->asc;
				flow->desc = node->desc;
			}
			else if (layout->error.code == TEX_ERR_OOM)
			{
				flow->failed = 1;
				return 0;
			}
		}
		break;
	case T_SPACE:
	case T_NEWLINE:
	case T_EOF:
		break;
	}

	return 1;
}

static int append_node(UnifiedPool* pool, TeX_Line* line, TexListBlock** tail, NodeRef ref)
{
	if (!*tail || (*tail)->count == TEX_LIST_BLOCK_CAP)
	{
		ListId id = pool_alloc_list_block(pool);
		if (id == LIST_NULL)
		{
			return 0;
		}

		if (*tail)
		{
			(*tail)->next = id;
		}
		else
		{
			line->content = id;
		}
		*tail = pool_get_list_block(pool, id);
	}
	(*tail)->items[(*tail)->count++] = ref;
	return 1;
}

int tex_flow_next(TeX_Flow* flow, TeX_Layout* layout, UnifiedPool* pool, TeX_Line* line, int retain)
{
	memset(line, 0, sizeof(*line));
	line->content = LIST_NULL;
	line->y = flow->y;
	line->asc = tex_metrics_asc(FONTROLE_MAIN);

	TexListBlock* tail = NULL;
	int x = 0;
	int desc = tex_metrics_desc(FONTROLE_MAIN);
	int has_content = 0;
	int pending_space = 0;
	int spaces = 0;

	while (flow_read(flow, layout, pool, retain))
	{
		TokenType type = flow->token.type;
		if (type == T_NEWLINE)
		{
			flow->has_token = 0;
			has_content = 1;
			break;
		}
		if (type == T_SPACE)
		{
			pending_space = 1;
			flow->has_token = 0;
			continue;
		}
		if (type != T_TEXT && flow->node == NODE_NULL)
		{
			flow->has_token = 0;
			continue;
		}

		int display = type == T_MATH_DISPLAY;
		int space = pending_space && has_content && !display
		    ? tex_metrics_text(" ", 1, FONTROLE_MAIN).w : 0;

		if (has_content && (display || x + space + flow->w > layout->width))
		{
			break; // keep the measured token and its original source position
		}

		if (space)
		{
			if (retain)
			{
				NodeRef ref = pool_alloc_node(pool);
				if (ref == NODE_NULL)
					goto oom;
				Node* node = pool_get_node(pool, ref);
				node->type = N_SPACE;
				node->w = (int16_t)space;
				node->asc = tex_metrics_asc(FONTROLE_MAIN);
				node->desc = tex_metrics_desc(FONTROLE_MAIN);
				if (!append_node(pool, line, &tail, ref))
					goto oom;
				++spaces;
			}
			x += space;
		}

		if (retain)
		{
			NodeRef ref = flow->node;
			if (type == T_TEXT)
			{
				ref = pool_alloc_node(pool);
				if (ref == NODE_NULL)
					goto oom;
				StringId sid = pool_alloc_string(pool, flow->token.start, (size_t)flow->token.len);
				if (sid == STRING_NULL)
					goto oom;
				Node* node = pool_get_node(pool, ref);
				node->type = N_TEXT;
				node->data.text.sid = sid;
				node->data.text.len = (uint16_t)flow->token.len;
				node->w = (int16_t)flow->w;
				node->asc = (int16_t)flow->asc;
				node->desc = (int16_t)flow->desc;
			}
			if (!append_node(pool, line, &tail, ref))
				goto oom;
		}

		x += flow->w;
		line->asc = TEX_MAX(line->asc, flow->asc);
		desc = TEX_MAX(desc, flow->desc);
		has_content = 1;
		pending_space = 0;
		flow->has_token = 0;
		if (display)
		{
			line->x_offset = TEX_MAX(0, (layout->width - flow->w) / 2);
			break;
		}
	}

	if (flow->failed || !has_content)
		return 0;

	if (layout->cfg.justify && spaces && flow->has_token &&
	    flow->token.type != T_MATH_DISPLAY)
	{
		int extra = layout->width - x;
		int each = extra / spaces;
		int remainder = extra - each * spaces;
		for (ListId id = line->content; id != LIST_NULL;)
		{
			TexListBlock* block = pool_get_list_block(pool, id);
			for (uint16_t i = 0; i < block->count; ++i)
			{
				Node* node = pool_get_node(pool, block->items[i]);
				if (node->type == N_SPACE)
				{
					node->w = (int16_t)(node->w + each + (remainder > 0));
					if (remainder > 0)
					{
						--remainder;
					}
				}
			}

			id = block->next;
		}
	}

	line->h = TEX_MAX(1, line->asc + desc + TEX_LINE_LEADING);
	if (line->h > TEX_MAX_TOTAL_HEIGHT - flow->y)
	{
		line->h = TEX_MAX_TOTAL_HEIGHT - flow->y;
		flow->eof = 1;
		flow->has_token = 0;
		TEX_SET_ERROR(layout, TEX_ERR_INPUT, "Document height limit exceeded", TEX_MAX_TOTAL_HEIGHT);
	}

	flow->y += line->h;
	return line->h > 0;

oom:
	flow->failed = 1;
	TEX_SET_ERROR(layout, TEX_ERR_OOM, "Failed to retain layout line", flow->y);
	return 0;
}

static void record_checkpoint(TeX_Layout* layout, const TeX_Flow* flow)
{
	int previous_y = layout->checkpoint_count
	    ? layout->checkpoints[layout->checkpoint_count - 1].y_pos : 0;
	if (flow->y - previous_y < TEX_CHECKPOINT_INTERVAL)
		return;

	if (layout->checkpoint_count == layout->checkpoint_capacity)
	{
		int capacity = layout->checkpoint_capacity ? layout->checkpoint_capacity * 2 : 8;
		TeX_Checkpoint* checkpoints = realloc(layout->checkpoints, (size_t)capacity * sizeof(*checkpoints));
		if (!checkpoints)
		{
			TEX_SET_ERROR(layout, TEX_ERR_OOM, "Failed to grow checkpoint array", capacity);
			return;
		}
		layout->checkpoints = checkpoints;
		layout->checkpoint_capacity = capacity;
	}

	TeX_Checkpoint* checkpoint = &layout->checkpoints[layout->checkpoint_count++];
	checkpoint->y_pos = flow->y;
	checkpoint->src_ptr = tex_flow_position(flow);
}

TeX_Layout* tex_format(const char* input, int width, const TeX_Config* config)
{
	if (!input || width <= 0 || !config)
	{
		return NULL;
	}

	TeX_Layout* layout = calloc(1, sizeof(*layout));
	if (!layout)
	{
		return NULL;
	}

	layout->cfg = *config;
	layout->width = width;
	layout->source = input;
	layout->source_end = input + strlen(input);

	for (int i = 0; i < 4; ++i)
	{
		for (int j = 0; j < i; ++j)
		{
			if (config->palette[i] == config->palette[j])
			{
				TEX_SET_ERROR(layout, TEX_ERR_INPUT, "Palette indices must be distinct", i);
				return layout;
			}
		}
	}

	tex_metrics_init(layout);
	if (layout->error.code != TEX_OK)
		return layout;

	UnifiedPool scratch;
	if (pool_init(&scratch, TEX_LAYOUT_SCRATCH_SIZE) != 0)
	{
		TEX_SET_ERROR(layout, TEX_ERR_OOM, "Failed to initialize scratch pool", 0);
		free(layout);
		return NULL;
	}

	TeX_Flow flow;
	TeX_Line line;
	tex_flow_init(&flow, layout, input, 0);

	while (tex_flow_next(&flow, layout, &scratch, &line, 0))
	{
		layout->total_height = flow.y;
		if (!flow.eof)
			record_checkpoint(layout, &flow);
	}

	pool_free(&scratch);
	return layout;
}

int tex_get_total_height(TeX_Layout* layout)
{
	if (!layout)
		return 0;
	return layout->total_height;
}

void tex_free(TeX_Layout* layout)
{
	if (!layout)
		return;

	if (layout->renderer)
	{
		tex_renderer_invalidate(layout->renderer);
	}
	tex_metrics_activate(NULL);

	free(layout->checkpoints);
	free(layout);
}

TeX_Error tex_get_last_error(TeX_Layout* layout)
{
	if (!layout)
		return TEX_ERR_INPUT;
	return (TeX_Error)layout->error.code;
}

const char* tex_get_error_message(TeX_Layout* layout)
{
	if (!layout)
		return "Invalid layout";
	return layout->error.msg ? layout->error.msg : "";
}

int tex_get_error_value(TeX_Layout* layout)
{
	if (!layout)
		return 0;
	return layout->error.val;
}
