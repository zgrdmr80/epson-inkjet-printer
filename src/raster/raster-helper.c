/*
   Copyright (C) Seiko Epson Corporation 2009.
 
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this program; if not, write to the Free  Software Foundation, Inc., 
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "raster-helper.h"
#include "scale.h"
#include "blend.h"
#include "mirror.h"
#include "reverse.h"

#define clamp_range(_value,_min,_max) { \
	if ((_value) < (_min)) {        \
		(_value) = (_min);      \
	} else if ((_value) > (_max)) { \
		(_value) = (_max);      \
	}                               \
}

static EpsRasterPipeline * pipeline_append_scale(EpsRasterPipeline * pipeline);
static EpsRasterPipeline * pipeline_append_watermark(EpsRasterPipeline * pipeline);
static EpsRasterPipeline * pipeline_append_mirror(EpsRasterPipeline * pipeline);
static EpsRasterPipeline * pipeline_append_reverse(EpsRasterPipeline * pipeline);

static EpsRasterPipeline * 
pipeline_append_pipe(EpsRasterPipeline * pipeline, EpsRasterPipe * pipe)
{
	debug_msg("%s:%d \t\t<<%s>>: \t\t Trace in \n", __FILE__, __LINE__, __FUNCTION__);
	if (pipeline && pipe) {
		debug_msg("%s:%d \t\t<<%s>>: \t\t step 1\n", __FILE__, __LINE__, __FUNCTION__);
		EpsRasterPipe ** pline_new = NULL;
		pline_new = (EpsRasterPipe **) eps_malloc(sizeof(EpsRasterPipe *) * (pipeline->numpipe + 1));
		if (pline_new) {
			debug_msg("%s:%d \t\t<<%s>>: \t\t step 2 \n", __FILE__, __LINE__, __FUNCTION__);
			if (pipeline->numpipe > 0) {
				debug_msg("%s:%d \t\t<<%s>>: \t\t step 3 \n", __FILE__, __LINE__, __FUNCTION__);
				int i;
				for(i = 0; i < pipeline->numpipe; i++) {
					debug_msg("%s:%d \t\t<<%s>>: \t\t for numpipe = %d\n", __FILE__, __LINE__, __FUNCTION__, i);
					pline_new[i] = pipeline->pipeline[i];
				}
				debug_msg("%s:%d \t\t<<%s>>: \t\t step 4\n", __FILE__, __LINE__, __FUNCTION__);
				eps_free(pipeline->pipeline);
			}
			debug_msg("%s:%d \t\t<<%s>>: \t\t step 5 \n", __FILE__, __LINE__, __FUNCTION__);
			pline_new[pipeline->numpipe] = pipe;
			pipeline->pipeline = pline_new;
			pipeline->numpipe++;

		}
	}
 	debug_msg("%s:%d \t\t<<%s>>: \t\t Trace out \n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline;
}


EpsRasterPipeline *
raster_helper_create_pipeline (EpsPageInfo * page, EpsRasterProcessMode process_mode)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in\n", __FILE__, __LINE__, __FUNCTION__);
	EpsRasterPipeline * pipeline = NULL;

	pipeline = (EpsRasterPipeline *)eps_malloc(sizeof(EpsRasterPipeline));
	if (pipeline) {
		debug_msg("%s:%d \t\t<<%s>>: Pipeline Processing Mode : %s\n", __FILE__, __LINE__, __FUNCTION__, (process_mode == EPS_RASTER_PROCESS_MODE_PRINTING) ? "PRINTING" : "FETCHING");
		debug_msg("%s:%d \t\t<<%s>>: bytes_per_pixel : %d\n", __FILE__, __LINE__, __FUNCTION__,page->bytes_per_pixel);
		debug_msg("%s:%d \t\t<<%s>>: src_print_area_x : %d\n", __FILE__, __LINE__, __FUNCTION__,page->src_print_area_x);
		debug_msg("%s:%d \t\t<<%s>>: src_print_area_y : %d\n", __FILE__, __LINE__, __FUNCTION__,page->src_print_area_y);
		debug_msg("%s:%d \t\t<<%s>>: prt_print_area_x : %d\n", __FILE__, __LINE__, __FUNCTION__,page->prt_print_area_x);
		debug_msg("%s:%d \t\t<<%s>>: prt_print_area_y : %d\n", __FILE__, __LINE__, __FUNCTION__,page->prt_print_area_y);
		debug_msg("%s:%d \t\t<<%s>>: scale : %d\n", __FILE__, __LINE__, __FUNCTION__,page->scale);
		debug_msg("%s:%d \t\t<<%s>>: mirror : %d\n", __FILE__, __LINE__, __FUNCTION__,page->mirror);
		debug_msg("%s:%d \t\t<<%s>>: reverse : %d\n", __FILE__, __LINE__, __FUNCTION__,page->reverse);
		debug_msg("%s:%d \t\t<<%s>>: watermark.use : %d\n", __FILE__, __LINE__, __FUNCTION__,page->watermark.use);

		memcpy(&pipeline->page, page, sizeof(EpsPageInfo));

		pipeline->process_mode = process_mode;
		pipeline->mode.duplecate = 1;
		pipeline->pipeline = NULL;
		pipeline->numpipe = 0;
		
		// Scale
		if (page->scale) {
			debug_msg("Pipeline Scale on\n");
			pipeline = pipeline_append_scale(pipeline);
		}

		// Watermark
		if (page->watermark.use == 1) {
			debug_msg("Pipeline Watermark on\n");
			pipeline = pipeline_append_watermark(pipeline);
		}
		
		// Mirror 
		if (page->mirror) {
			debug_msg("Pipeline Mirror on\n");
			pipeline = pipeline_append_mirror(pipeline);
		}

		// Reverse
		if (page->reverse) {
			debug_msg("Pipeline Reverse on\n");
			pipeline = pipeline_append_reverse(pipeline);
			pipeline->mode.duplecate = 0;
		}
	}
	debug_msg("%s:%d \t\t<<%s>>: Trace out\n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline;
}

void
raster_helper_destroy_pipeline (EpsRasterPipeline * pipeline)
{
	if (pipeline) {
		if (pipeline->pipeline) {
			eps_free(pipeline->pipeline);
		}
		eps_free(pipeline);
	}
}

/* Static function to create each pipe */
#define PIPE_INIT(pipe, init_p, func) {		 		\
	pipe->self = pipe;					\
	pipe->opt = (PIPEOPT) init_p;				\
	pipe->obj = NULL;					\
	pipe->output = NULL;					\
	pipe->output_h = NULL;					\
	pipe->pipe_init = eps_init_ ## func;			\
	pipe->pipe_process = eps_process_ ## func;		\
	pipe->pipe_free = eps_free_ ## func;			\
}

static EpsRasterPipeline * 
pipeline_append_scale(EpsRasterPipeline * pipeline)
{
	debug_msg("%s:%d \t\t<<%s>>: start AppendScale()\n", __FILE__, __LINE__, __FUNCTION__);
	EpsRasterPipe * pipe = (EpsRasterPipe *) eps_malloc(sizeof(EpsRasterPipe));
	if (pipe) {
		debug_msg("%s:%d \t\t<<%s>>: init scaleOpt()\n", __FILE__, __LINE__, __FUNCTION__);
		EpsScaleOpt * init_p = (EpsScaleOpt *)eps_malloc(sizeof(EpsScaleOpt));
		if (init_p) {
			init_p->do_scaling = pipeline->page.scale;
			init_p->bytes_per_pixel = pipeline->page.bytes_per_pixel;
			init_p->src_print_area_x = pipeline->page.src_print_area_x;
			init_p->src_print_area_y = pipeline->page.src_print_area_y;
			init_p->prt_print_area_x = pipeline->page.prt_print_area_x;
			init_p->prt_print_area_y = pipeline->page.prt_print_area_y;
			init_p->pipe = pipe;
			debug_msg("%s:%d \t\t<<%s>>: \t\t do_scaling = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->do_scaling);
			debug_msg("%s:%d \t\t<<%s>>: \t\t bytes_per_pixel = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->bytes_per_pixel);
			debug_msg("%s:%d \t\t<<%s>>: \t\t src_print_area_x = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->src_print_area_x);
			debug_msg("%s:%d \t\t<<%s>>: \t\t src_print_area_x = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->src_print_area_y);
			debug_msg("%s:%d \t\t<<%s>>: \t\t prt_print_area_x = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->prt_print_area_x);
			debug_msg("%s:%d \t\t<<%s>>: \t\t prt_print_area_y = %d()\n", __FILE__, __LINE__, __FUNCTION__, init_p->prt_print_area_y);
			debug_msg("%s:%d \t\t<<%s>>: \t\t pipe = %p()\n", __FILE__, __LINE__, __FUNCTION__, init_p->pipe);
			PIPE_INIT(pipe, init_p, scale);
		} else {
			eps_free(pipe);
			pipe = NULL;
		}
	}
	debug_msg("%s:%d \t\t<<%s>>: \t\t pipeline_append_pipe()\n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline_append_pipe(pipeline, pipe);
}

static EpsRasterPipeline * 
pipeline_append_watermark(EpsRasterPipeline * pipeline)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in()\n", __FILE__, __LINE__, __FUNCTION__);
	const float watermarkDensitys [] = {
		0.95, /* Level 1 Light */
		0.9,  /* Level 2 */
		0.8,  /* Level 3 */
		0.75, /* Level 4 */
		0.3,  /* Level 5 */
		0.25, /* Level 6 Dark */
	};
	const int numDensityElems = sizeof(watermarkDensitys) / sizeof(watermarkDensitys[0]);

	const EpsColor watermarkColors [] = {
		{ 0, 0, 0, 0 }, /* Black   */
		{ 0, 0, 1, 0 }, /* Blue    */
		{ 0, 1, 0, 0 }, /* Lime    */
		{ 0, 1, 1, 0 }, /* Aqua    */
		{ 1, 0, 0, 0 }, /* Red     */
		{ 1, 0, 1, 0 }, /* Fuchsia */
		{ 1, 1, 0, 0 }, /* Yellow  */
	};
	const int numColorsElems = sizeof(watermarkColors) / sizeof(watermarkColors[0]);

	EpsRasterPipe * pipe = (EpsRasterPipe *) eps_malloc(sizeof(EpsRasterPipe));
	EpsRect frame;
	EpsRect bounds;
	float ratio;
	int color_index;
	int density_index;
	if (pipe) {
		EpsBlendOpt * init_p = (EpsBlendOpt *)eps_malloc(sizeof(EpsBlendOpt));
		if (init_p) {
			ratio = pipeline->page.watermark.size_ratio;
			clamp_range(ratio, 0, 1.0);

			frame = epsMakeRect(0, 0, pipeline->page.prt_print_area_x, pipeline->page.prt_print_area_y);
			bounds.size.width = (float) frame.size.width * ratio;
			bounds.size.height = (float) frame.size.height * ratio;
			switch (pipeline->page.watermark.position) {
				case EPS_PAGE_WATERMARK_POSITION_CENTER:
					bounds.origin.x = (frame.size.width - bounds.size.width) / 2;
					bounds.origin.y = (frame.size.height - bounds.size.height) / 2;
					break;
				case EPS_PAGE_WATERMARK_POSITION_TOPLEFT:
					bounds.origin.x = 0;
					bounds.origin.y = 0;
					break;
				case EPS_PAGE_WATERMARK_POSITION_TOP:
					bounds.origin.x = (frame.size.width - bounds.size.width) / 2;
					bounds.origin.y = 0;
					break;
				case EPS_PAGE_WATERMARK_POSITION_TOPRIGHT:
					bounds.origin.x = frame.size.width - bounds.size.width;
					bounds.origin.y = 0;
					break;
				case EPS_PAGE_WATERMARK_POSITION_LEFT:
					bounds.origin.x = 0;
					bounds.origin.y = (frame.size.height - bounds.size.height) / 2;
					break;
				case EPS_PAGE_WATERMARK_POSITION_RIGHT:
					bounds.origin.x = frame.size.width - bounds.size.width;
					bounds.origin.y = (frame.size.height - bounds.size.height) / 2;
					break;
				case EPS_PAGE_WATERMARK_POSITION_BOTTOMLEFT:
					bounds.origin.x = 0;
					bounds.origin.y = frame.size.height - bounds.size.height;
					break;
				case EPS_PAGE_WATERMARK_POSITION_BOTTOM:
					bounds.origin.x = (frame.size.width - bounds.size.width) / 2;
					bounds.origin.y = frame.size.height - bounds.size.height;
					break;
				case EPS_PAGE_WATERMARK_POSITION_BOTTOMRIGHT:
					bounds.origin.x = frame.size.width - bounds.size.width;
					bounds.origin.y = frame.size.height - bounds.size.height;
					break;
				deafult:
					break;
			}

			init_p->source_path = pipeline->page.watermark.filepath;
			init_p->source_type = EPS_BLEND_SOURCE_TYPE_WATERMARK;

			color_index = pipeline->page.watermark.color;
			clamp_range(color_index, 0, numColorsElems);

			if (pipeline->page.bytes_per_pixel == 3) { /* RGB */
				init_p->color = watermarkColors[color_index];
			} else { /* Grayscale */
				init_p->color.red = 0;
				init_p->color.green = 0;
				init_p->color.blue = 0;
			}

			density_index = pipeline->page.watermark.density;
			clamp_range(density_index, 0, numDensityElems);
			init_p->color.alpha = 1.0 - watermarkDensitys[density_index];

			init_p->frame = frame;
			init_p->bounds = bounds;
			init_p->pipe = pipe;
			PIPE_INIT(pipe, init_p, blend);

			debug_msg("source_path : %s", init_p->source_path);
			debug_msg("source_type : %d", init_p->source_type);
			debug_msg("frame (%d, %d, %d, %d)", init_p->frame.origin.x, init_p->frame.origin.y, init_p->frame.size.width, init_p->frame.size.height);
			debug_msg("bounds (%d, %d, %d, %d)", init_p->bounds.origin.x, init_p->bounds.origin.y, init_p->bounds.size.width, init_p->bounds.size.height);
			debug_msg("color (%d, %d, %d, %d)", init_p->color.red, init_p->color.green, init_p->color.blue, init_p->color.alpha);
		} else {
			eps_free(pipe);
			pipe = NULL;
		}
	}
	debug_msg("%s:%d \t\t<<%s>>: Trace_out()\n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline_append_pipe(pipeline, pipe);
}

static EpsRasterPipeline *
pipeline_append_mirror(EpsRasterPipeline * pipeline)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in\n", __FILE__, __LINE__, __FUNCTION__);
	EpsRasterPipe * pipe = (EpsRasterPipe *) eps_malloc(sizeof(EpsRasterPipe));
	if (pipe) {
		EpsMirrorOpt * init_p = (EpsMirrorOpt *)eps_malloc(sizeof(EpsMirrorOpt));
		if (init_p) {
			init_p->bytes_per_pixel = pipeline->page.bytes_per_pixel;
			init_p->pipe = pipe;
			PIPE_INIT(pipe, init_p, mirror);
		} else {
			eps_free(pipe);
			pipe = NULL;
		}
	}
	debug_msg("%s:%d \t\t<<%s>>: Trace out()\n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline_append_pipe(pipeline, pipe);
}

static EpsRasterPipeline * 
pipeline_append_reverse(EpsRasterPipeline * pipeline)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in()\n", __FILE__, __LINE__, __FUNCTION__);
	EpsRasterPipe * pipe= (EpsRasterPipe *) eps_malloc(sizeof(EpsRasterPipe));
	if (pipe) {
		EpsReverseOpt * init_p = (EpsReverseOpt *)eps_malloc(sizeof(EpsReverseOpt));
		if (init_p) {
			init_p->bytes_per_pixel = pipeline->page.bytes_per_pixel;
			init_p->bytes_per_raster = pipeline->page.prt_print_area_x * pipeline->page.bytes_per_pixel;
			init_p->top_margin = pipeline->page.src_print_area_y - pipeline->page.prt_print_area_y;
			init_p->num_raster = pipeline->page.prt_print_area_y;
			init_p->pipe = pipe;
			PIPE_INIT(pipe, init_p, reverse);

			debug_msg("top_margin (%d)", init_p->top_margin);
			debug_msg("num_raster (%d)", init_p->num_raster);

		} else {
			eps_free(pipe);
			pipe = NULL;
		}
	}
	debug_msg("%s:%d \t\t<<%s>>: Trace out()\n", __FILE__, __LINE__, __FUNCTION__);
	return pipeline_append_pipe(pipeline, pipe);
}

