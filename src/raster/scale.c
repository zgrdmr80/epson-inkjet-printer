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
#include "scale.h"

typedef int (* ScaleMethodStart) (SCALE);
typedef int (* ScaleMethodRasterOut) (SCALE, char*, int, int, int*);
typedef int (* ScaleMethodEnd) (SCALE);

typedef struct EpsScale {
	EpsScaleOpt * init_data;
	int raster_index;
	float x_scale;
	float y_scale;
	
	void * method_data;
	ScaleMethodStart start;
	ScaleMethodRasterOut rasterout;
	ScaleMethodEnd end;
} EpsScale;

//  Scaling not effected just as original pixels returned.
static int
scale_start_unchanged (SCALE scale)
{
	return 0;
}

static int
scale_rasterout_unchanged (SCALE scale, char * raster, int bytes, int pixels, int * outraster)
{
	EpsScale * lp_scale = (EpsScale *) scale;
	EpsScaleOpt * lp_data = (EpsScaleOpt *) lp_scale->init_data;

	int nraster = 0;
	int error = lp_data->pipe->output(lp_data->pipe->output_h, raster, bytes, pixels, &nraster);
	if (error == 0) {
		*outraster = nraster;
	}

	return error;
}

static int
scale_end_unchanged (SCALE scale)
{
	return 0;
}

// Use nearest neighbour interpolation
typedef struct _MethodNearest {
	float scale;
	char * scaled_p;
	int scaled_bytes;
	int scaled_pixels;
	float print_one_more;
} MethodNearest;

static int
scale_start_nearest (SCALE scale)
{
	int eps_error = 0;
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t Trace in\n", __FILE__, __LINE__, __FUNCTION__);
	EpsScale * lp_scale = (EpsScale *) scale;
	EpsScaleOpt * lp_data = (EpsScaleOpt *) lp_scale->init_data;
	MethodNearest * p = (MethodNearest *) eps_malloc(sizeof(MethodNearest));
	

	if (p) {
		p->print_one_more = 0.0f;
		if (lp_scale->x_scale < lp_scale->y_scale) {
			p->scale = lp_scale->x_scale;
		} else {
			p->scale = lp_scale->y_scale;
		}
		p->scaled_pixels = lp_data->src_print_area_x * p->scale;
		p->scaled_bytes = p->scaled_pixels * lp_data->bytes_per_pixel;
		p->scaled_p = (char *) eps_malloc(p->scaled_bytes);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t Scale method nearest byte= %d, %d\n", __FILE__, __LINE__, __FUNCTION__, p->scaled_bytes, *(p->scaled_p));
		if (p->scaled_p == NULL) {
			eps_error = 1;
		}

		lp_scale->method_data = (void *) p;
		MethodNearest * lp_method = (MethodNearest *) lp_scale->method_data;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t Scale method nearest = %p\n", __FILE__, __LINE__, __FUNCTION__, lp_method->scaled_p);
	} else {
		eps_error = 1;
	}
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t Trace out\n", __FILE__, __LINE__, __FUNCTION__);
	return eps_error;
}

static int
scale_rasterout_nearest (SCALE scale, char * raster, int bytes, int pixels, int * outraster)
{
	EpsScale * lp_scale = (EpsScale *) scale;
	EpsScaleOpt * lp_data = (EpsScaleOpt *) lp_scale->init_data;
	MethodNearest * lp_method = (MethodNearest *) lp_scale->method_data;

	char * scaled_p = lp_method->scaled_p;
	int scaled_bytes = lp_method->scaled_bytes;
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t Scale rasterout scaled_bytes = %d\n", __FILE__, __LINE__, __FUNCTION__, scaled_bytes);
	int scaled_pixels = lp_method->scaled_pixels;
	int printable_lines = lp_method->scale;
	int bpp = lp_data->bytes_per_pixel;

	lp_method->print_one_more += (lp_method->scale - printable_lines);
	if (lp_method->print_one_more >= 1.0f) {
		printable_lines++;
		lp_method->print_one_more -= 1.0f;
	}

	if (printable_lines > 0) {
		memset(scaled_p, 0xff, scaled_bytes);

		int i;
		char * p = scaled_p;
		float one_more_pixel = 0.0f;

		for (i = 0; i < pixels; i++) {
			int copy_pixels = lp_method->scale;
			one_more_pixel += (lp_method->scale - copy_pixels);
			if (one_more_pixel >= 1.0f) {
				copy_pixels++;
				one_more_pixel -= 1.0f;
			}

			while (copy_pixels > 0) {
				memcpy(p, raster + (i * bpp), bpp);
				p += bpp;
				copy_pixels--;
			}
		}
	}

	int eps_error = 0;
	*outraster = 0;
	while (printable_lines > 0 && eps_error == 0) {
		int nraster = 0;
		eps_error = lp_data->pipe->output(lp_data->pipe->output_h, scaled_p, scaled_bytes, scaled_pixels, &nraster);
		printable_lines--;
		*outraster += nraster;
	}

	return eps_error;
}

static int
scale_end_nearest (SCALE scale)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in \n", __FILE__, __LINE__, __FUNCTION__);
	EpsScale * lp_scale = (EpsScale *) scale;
	MethodNearest * lp_method = (MethodNearest *) lp_scale->method_data;

	// cleaning up own method data
	if (lp_method->scaled_p) {
		eps_free(lp_method->scaled_p);
	}

	eps_free(lp_method);
	debug_msg("%s:%d \t\t<<%s>>: Trace out \n", __FILE__, __LINE__, __FUNCTION__);
	return 0;
}


///////////////////////////////////////////////////////////////////////////////
//
// * A P I for scale (extern functions)
//
///////////////////////////////////////////////////////////////////////////////
#define SCALE_SETFUNC(p, func) {			\
	p->start = scale_start_ ## func;		\
	p->rasterout = scale_rasterout_ ## func;	\
	p->end = scale_end_ ## func;			\
}

int
eps_init_scale (RASTERPIPE * scale_p, PIPEOPT init_p)
{
	int eps_error = 0;
	EpsScale * p;

	p = (EpsScale *) eps_malloc(sizeof(EpsScale));
	if (p && init_p) {
		p->init_data = (EpsScaleOpt *) init_p;

		if (p->init_data->do_scaling) {
			p->raster_index = 0;
			p->x_scale = (float) p->init_data->prt_print_area_x / (float) p->init_data->src_print_area_x;
			p->y_scale = (float) p->init_data->prt_print_area_y / (float) p->init_data->src_print_area_y;
			
			debuglog(("src print area x : %d", p->init_data->src_print_area_x));
			debuglog(("src print area y : %d", p->init_data->src_print_area_y));
			debuglog(("prt print area x : %d", p->init_data->prt_print_area_x));
			debuglog(("prt print area y : %d", p->init_data->prt_print_area_y));
			debuglog(("scale (%.2f, %.2f)", p->x_scale, p->y_scale));

			SCALE_SETFUNC(p, nearest);
		} else {
			debuglog(("scale not effected just input raster will return"));
			SCALE_SETFUNC(p, unchanged);
		}

		eps_error = p->start(p);

		*scale_p = (SCALE) p;

	} else {
		eps_error = 1;
	}

	return eps_error;
}

int
eps_process_scale (RASTERPIPE scale, char* raster_p, int raster_bytes, int pixel_num, int * outraster)
{
	EpsScale * lp_scale = (EpsScale *) scale;
	EpsScaleOpt * lp_data = (EpsScaleOpt *) lp_scale->init_data;
	int error = 0;
	int nraster = 0;

	*outraster = 0;
	if (raster_p) {
		error = lp_scale->rasterout(lp_scale, raster_p, raster_bytes, pixel_num, &nraster);
		if (error == 0) {
			*outraster = nraster;
		}
	} else {
		debuglog(("SCALE FLUSHING HERE ..."));
		lp_data->pipe->output(lp_data->pipe->output_h, NULL, 0, 0, &nraster);
	}

	return error;
}

int
eps_free_scale (RASTERPIPE scale)
{
	debuglog(("TRACE IN eps_free_scale function"));
	EpsScale * lp_scale = (EpsScale *) scale;

	if (lp_scale) {
	debuglog(("IN: if(lp_scale)"));
		lp_scale->end(lp_scale);
	debuglog(("TEST"));
		if (lp_scale->init_data) {
		debuglog(("IN: if (lp_scale->init_data)"));
			eps_free(lp_scale->init_data);
		}
		debuglog(("OUT: if (lp_scale->init_data)"));
		eps_free(lp_scale);
	debuglog(("OUT: if(lp_scale)"));
	}
	debuglog(("TRACE OUT eps_free_scale function"));
	return 0;
}

