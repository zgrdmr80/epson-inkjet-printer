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
#include "raster.h"
#include "fetch-pool.h"

typedef struct EpsRaster {
	HANDLE drv_handle;
	RASTEROUT_FUNC output;
	PIPEOUT_FUNC pipeout;
	EpsRasterPipeline * pipeline;
	char * input_raster;
	int input_raster_index;
	char * output_raster;
	int output_raster_index;
	FETCHPOOL fetchpool;
} EpsRaster;

static int
output_to_printer(PIPEOUT_HANDLE handle, char * raster_p, int raster_bytes, int pixel_num, int * outraster)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in \n", __FILE__, __LINE__, __FUNCTION__);
	EpsRaster * raster = (EpsRaster *) handle;
	int r_bytes = 0;
	int r_pixels = 0;
	int error = 0;
	char * r_ptr = NULL;
	
	*outraster = 0;

	if (raster && raster_p) {
		raster->output_raster_index++;
		r_bytes = raster->pipeline->page.prt_print_area_x * raster->pipeline->page.bytes_per_pixel;
		r_pixels = raster->pipeline->page.prt_print_area_x;
		debug_msg("%s:%d \t\t<<%s>>: r_bytes = %d\n", __FILE__, __LINE__, __FUNCTION__, r_bytes);
		debug_msg("%s:%d \t\t<<%s>>: r_pixels = %d\n", __FILE__, __LINE__, __FUNCTION__, r_pixels);

		if (raster->output_raster_index <= raster->pipeline->page.prt_print_area_y) {
			if (raster_bytes >= r_bytes) { 
				r_ptr = raster_p;
			} else {
				memset(raster->output_raster, 0xFF, r_bytes);
				memcpy(raster->output_raster, raster_p, raster_bytes);
				r_ptr = raster->output_raster;
			}

#ifdef DEBUG_VERBOSE
			debuglog(("line index : %d (error=%d)", raster->output_raster_index, error));
#endif
			error = raster->output(raster->drv_handle, r_ptr, r_bytes, r_pixels);
		}

		if (error == 0) {
			*outraster = 1;
		}
	} else {
		debug_msg("Filter_PRINTER (end of raster pipeline) FLUSHING HERE ...\n");
		debug_msg("%s:%d \t\t<<%s>>: raster = %p, raster_p = %p \n", __FILE__, __LINE__, __FUNCTION__, raster, raster_p);
	}
	debug_msg("%s:%d \t\t<<%s>>: Trace out \n", __FILE__, __LINE__, __FUNCTION__);
	return error;
}

static int
output_to_fetchpool(PIPEOUT_HANDLE handle, char * raster_p, int raster_bytes, int pixel_num, int * outraster)
{
	EpsRaster * raster = (EpsRaster *) handle;
	EpsFetchData data = { 0 } ;
	FETCHPOOL * pool = NULL;
	int error = 1;

	do {
		*outraster = 0;
		if (raster == NULL) {
			break;
		}

		pool = raster->fetchpool;
		if (pool == NULL) {
			break;
		}

		data.duplicate = raster->pipeline->mode.duplecate;
		data.raster_p = raster_p;
		data.raster_bytes = raster_bytes;
		data.pixel_num = pixel_num;

		error = fetchpool_add_data(pool, &data);
		if (error) {
			break;
		}

		*outraster = 1;

		error = 0;

	} while (0);

	return error;
}

static int
pipeline_init_all(EpsRasterPipeline * pipeline, PIPEOUT_FUNC output, PIPEOUT_HANDLE output_h)
{

	int error = 0;
	int i;
	EpsRasterPipe * p;
	debug_msg("%s:%d \t\t<<%s>>: Trace in\n", __FILE__, __LINE__, __FUNCTION__);
	debug_msg(" number pipe : %d \n", pipeline->numpipe);
	debug_msg(" pipeline output = %#x, output_h = %#x \n", output, output_h);

	for (i = 0; i < pipeline->numpipe; i++) {
		p = pipeline->pipeline[i];
		error = p->pipe_init(&p->obj, p->opt);
		if (error) {
			break;
		}
	}

	if (error == 0) {
		for (i = 0; i < pipeline->numpipe; i++) {
			p = pipeline->pipeline[i];

			if (i < pipeline->numpipe - 1) {
				EpsRasterPipe * next = pipeline->pipeline[i + 1];
				p->output = next->pipe_process;
				p->output_h = next->obj;
			} else {
				p->output = output;
				p->output_h = output_h;
			}

			debug_msg(" p->output = %#x, p->output_h = %#x \n", p->output, p->output_h);
			debug_msg(" pipe %d init = %s \n", i + 1, (error) ? "failed" : "succeed");
		}
	}

	return error;
}

int
eps_raster_init (RASTER * handle, EpsRasterOpt * data, EpsRasterPipeline * pipeline)
{
	debug_msg("%s:%d \t\t<<%s>>: Trace in\n", __FILE__, __LINE__, __FUNCTION__);
	EpsRaster * p = NULL;
	PIPEOUT_FUNC pipeout_func = NULL;
	int error = 1;

	do {
		if (data == NULL) {
			debug_msg("%s:%d \t\t<<%s>>: step 1\n", __FILE__, __LINE__, __FUNCTION__);
			break;
		}

		if (pipeline == NULL) {
			debug_msg("%s:%d \t\t<<%s>>: step 2\n", __FILE__, __LINE__, __FUNCTION__);
			break;
		}

		p = (EpsRaster *) eps_malloc(sizeof(EpsRaster));
		if (p == NULL) {
			debug_msg("%s:%d \t\t<<%s>>: step 3\n", __FILE__, __LINE__, __FUNCTION__);
			break;
		} 
		
		debug_msg("%s:%d \t\t<<%s>>: step 4\n", __FILE__, __LINE__, __FUNCTION__);
		p->drv_handle = data->drv_handle;
		p->output = data->raster_output;
		p->pipeline = pipeline;

		if (p->pipeline->process_mode == EPS_RASTER_PROCESS_MODE_FETCHING) {
			debug_msg("%s:%d \t\t<<%s>>: FETCHING MODE\n", __FILE__, __LINE__, __FUNCTION__);
			pipeout_func = output_to_fetchpool;
			debug_msg("%s:%d \t\t<<%s>>: start fetchpool_create_instance() \n", __FILE__, __LINE__, __FUNCTION__);
			p->fetchpool = fetchpool_create_instance(pipeline->page.prt_print_area_y);
			if (p->fetchpool == NULL) {
				break;
			}
		} else { /* PRINTING */
			debug_msg("%s:%d \t\t<<%s>>: PRINTING MODE\n", __FILE__, __LINE__, __FUNCTION__);
			pipeout_func = output_to_printer;
			p->fetchpool = NULL;
		}
		error = pipeline_init_all(p->pipeline, pipeout_func, p);
		if (error) {
			break;
		}
		p->pipeout = pipeout_func;

		p->input_raster_index = 0;
		p->input_raster = (char *)eps_malloc(pipeline->page.src_print_area_x * pipeline->page.bytes_per_pixel);
		if (p->input_raster == NULL) {
			break;
		}
		p->output_raster_index = 0;
		p->output_raster = (char *)eps_malloc(pipeline->page.prt_print_area_x * pipeline->page.bytes_per_pixel);
		if (p->output_raster == NULL) {
			break;
		}

		*handle = (RASTER) p;

		error = 0;

	} while (0);

	debug_msg("%s:%d \t\t<<%s>>: Trace out\n", __FILE__, __LINE__, __FUNCTION__);
	return error;
}

/* if raster_p equals NULL means that it is need to flush a page. */
int
eps_raster_print (RASTER handle, char * raster_p, int raster_bytes, int pixel_num, int * outraster)
{
	EpsRaster * raster = (EpsRaster *) handle;
	EpsRasterPipeline * pipeline = NULL;
	EpsRasterPipe * first_pipe = NULL;
	char * r_ptr = NULL;
	int r_bytes = 0;
	int r_pixels = 0;
	int nraster = 0;
	int error = 0;
	
	*outraster = 0;

	if (raster && raster->input_raster_index <= raster->pipeline->page.src_print_area_y) {
		pipeline = raster->pipeline;
		if (raster_p) {
			r_bytes = raster->pipeline->page.src_print_area_x * raster->pipeline->page.bytes_per_pixel;
			r_pixels = raster->pipeline->page.src_print_area_x;

			if (raster_bytes >= r_bytes) { 
				r_ptr = raster_p;
			} else {
				memset(raster->input_raster, 0xFF, r_bytes);
				memcpy(raster->input_raster, raster_p, raster_bytes);
				r_ptr = raster->input_raster;
			}
		} else { // flushing
			r_ptr = NULL;
			r_bytes = 0;
			r_pixels = 0;
		}

		if (pipeline && pipeline->numpipe) {
			first_pipe = pipeline->pipeline[0]; // first pipe
			error = first_pipe->pipe_process(first_pipe->obj, r_ptr, r_bytes, r_pixels, &nraster);
			if (error == 0) {
				*outraster = nraster;
			}
		} else {
			error = raster->pipeout(raster, r_ptr, r_bytes, r_pixels, &nraster);
			if (error == 0) {
				*outraster = 1;
			}
		}
	}

	return error;
}

/* if fetched_p equals NULL means that check fetching status. */
int
eps_raster_fetch (RASTER handle, char * fetch_p, int fetch_bytes, int fetch_pixels, EpsRasterFetchStatus * current_status)
{
	EpsRaster * raster = (EpsRaster *) handle;
	FETCHPOOL * pool = NULL;
	EpsRasterFetchStatus inner_status;
	EpsFetchData * data = NULL;
	int error = 1;

	do {
		if (raster == NULL) {
			break;
		}

		pool = raster->fetchpool;
		if (pool == NULL) {
			break;
		}

		if (fetch_p == NULL) { /* just return inner status of fetch pool */
			fetchpool_get_status(pool, &inner_status);
			*current_status = inner_status;
			error = 0;
			break;
		}

		data = fetchpool_fetch_data(pool);
		if (data == NULL) {
			break;
		}

		if (fetch_bytes >= data->raster_bytes) {
			fetch_bytes = data->raster_bytes;
		}

		memset(fetch_p, 0xFF, fetch_bytes);
		memcpy(fetch_p, data->raster_p, fetch_bytes);

		error = 0;

	} while (0);

	return error;
}

int eps_raster_free (RASTER handle)
{
	debuglog(("TRACE IN eps_raster_free/raster.c"));
	EpsRaster * raster = (EpsRaster *) handle;
	EpsRasterPipe * p = NULL;
	EpsRasterPipeline * pipeline = NULL;
	int error = 0;
	int i;

	if (raster) {
	debuglog (("IN: if (raster)"));
		pipeline = raster->pipeline;
		if (pipeline) {
		debuglog (("IN: if (pipeline)"));
			for (i = 0; i < pipeline->numpipe; i++) {
				p = pipeline->pipeline[i];
				if (p && p->obj) {
				debuglog (("IN: if (p && p -> obj)"));
					p->pipe_free(p->obj);
					debuglog (("p->pipe_free(p->obj)"));	
					eps_free(p);
				debuglog (("OUT: if (p && p -> obj)"));
				}
			}
		debuglog (("OUT: if (pipeline)"));
		}
	debuglog (("OUT: if (raster)"));
		if (raster->input_raster) {
	debuglog (("IN: if (raster->input_raster)"));
			eps_free(raster->input_raster);
		}
	debuglog (("OUT: if (raster->input_raster)"));
		if (raster->output_raster) {
	debuglog (("IN: if (raster->output_raster)"));
			eps_free(raster->output_raster);
		}
	debuglog (("OUT: if (raster->output_raster)"));

		if (raster->fetchpool) {
	debuglog (("IN: if (raster->fetchpool)"));
			fetchpool_destroy_instance(raster->fetchpool);
		}
	debuglog (("OUT: if (raster->fetchpool)"));
		eps_free(raster);
	}
	debuglog(("TRACE OUT eps_raster_free/raster.c"));
	return error;
}
