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
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dlfcn.h>

#include <cups/cups.h>
#include <cups/ppd.h>
#include <cups/raster.h>

#include "raster.h"
#include "memory.h"
#include "raster_to_epson.h"
#include "pagemanager.h"
#include "filter_option.h"
#include "raster-helper.h"

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

#define safeFree(ptr,releaseFunc) {	\
	if ((ptr) != NULL) { 		\
		releaseFunc((ptr)); 	\
		(ptr) = NULL; 		\
	} 				\
}

extern cups_raster_t * 		Raster;
extern ppd_file_t * 		PPD;
extern char			JobName[EPS_JOBNAME_BUFFSIZE];
extern const char *		JobOptions;
extern int			JobCanceled;

static EPCGInitialize		epcgInitialize = NULL;
static EPCGRelease		epcgRelease = NULL;
static EPCGGetVersion		epcgGetVersion = NULL; 
static EPCGSetResource		epcgSetResource = NULL;
static EPCGGetOptionList	epcgGetOptionList = NULL;
static EPCGGetChoiceList	epcgGetChoiceList = NULL;
static EPCGSetPrintOption	epcgSetPrintOption = NULL;
static EPCGGetPageAttribute	epcgGetPageAttribute = NULL;
static EPCGStartJob		epcgStartJob = NULL;
static EPCGStartPage		epcgStartPage = NULL;
static EPCGRasterOut		epcgRasterOut = NULL;
static EPCGEndPage		epcgEndPage = NULL;
static EPCGEndJob		epcgEndJob = NULL;

#if DEBUG
static int page_no = 0;
static int pageHeight = 0;
#endif

int CustomSize = 0;
int ReduceEnlag = 0;
int OutputPaperSize = 0;
int ReduceEnlagPer = 0;
int checkReduceEnlagPer = 0;

int rasterSource(char *buf, int bufSize)
{
	int readBytes = 0;
	if (JobCanceled == 0) {
		readBytes = cupsRasterReadPixels(Raster, buf, bufSize);
	} else {
		readBytes = (-1); /* error */
	} 

	return readBytes;
}

static void * memAlloc(size_t size)
{
	return malloc(size);
}

static void memFree(void * ptr)
{
	free(ptr);
}

static EPS_INT32 getLocalTime (EPS_LOCAL_TIME * epsTime)
{
	time_t now;
	struct tm *t;

	now = time(NULL);
	t = (struct tm *)localtime(&now);

	epsTime->year = (EPS_UINT16)t->tm_year + 1900;
	epsTime->mon = (EPS_UINT8)t->tm_mon + 1;
	epsTime->day = (EPS_UINT8)t->tm_mday;
	epsTime->hour = (EPS_UINT8)t->tm_hour;
	epsTime->min = (EPS_UINT8)t->tm_min;
	epsTime->sec = (EPS_UINT8)t->tm_sec;

	return 0;	
}

static EPS_INT32 resOpen(EPS_INT8* resPath)
{
	return open(resPath, O_RDONLY);
}

static EPS_INT32 resRead(EPS_INT32 fd, EPS_INT8* buffer, EPS_INT32 bufSize)
{
	return read(fd, buffer, bufSize);
}

static EPS_INT32 resSeek(EPS_INT32 fd, EPS_INT32 offset, EPS_SEEK origin)
{
	int seek;
	switch(origin) {
		case EPS_SEEK_SET: seek = SEEK_SET; break;
		case EPS_SEEK_CUR: seek = SEEK_CUR; break;
		case EPS_SEEK_END: seek = SEEK_END; break;
		default: break;
	}
	return lseek(fd, offset, seek);
}

static EPS_INT32 resClose(EPS_INT32 fd)
{
	return close(fd);
}


static EPS_INT32 printStream (EPS_INT8* data, EPS_INT32 size)
{
	return fwrite(data, 1, size, stdout);
}

static int pipeOut(HANDLE handle, char* data, int dataSize, int pixelCount)
{
	(void) handle;

#if DEBUG
	pageHeight++;
#endif

	return epcgRasterOut(data, dataSize, pixelCount);
}

static int load_core_library (HANDLE * handle)
{
	debuglog(("TRACE IN"));
	
	HANDLE lib_handle = NULL;
	int error = 1;
	ppd_attr_t * attr = NULL;
	EPS_RES_FUNC resFunc;
	char library [PATH_MAX];

	do {
		attr = get_ppd_attr ("epcgCoreLibrary", 1);
		if (attr == NULL) {
			break;
		}

		snprintf(library, sizeof(library), "%s/%s", CORE_LIBRARY_PATH, attr->value);
		lib_handle = dlopen(library, RTLD_LAZY);
		if (lib_handle == NULL) {
			debuglog(("Failed to dlopen(%s)->%s", attr->value, dlerror()));
			break;
		}

		/* Setting of library function */
		epcgInitialize = (EPCGInitialize) dlsym (lib_handle, "epcgInitialize");
		epcgRelease = (EPCGRelease) dlsym (lib_handle, "epcgRelease");
		epcgGetVersion = (EPCGGetVersion) dlsym (lib_handle, "epcgGetVersion");
		epcgSetResource = (EPCGSetResource) dlsym (lib_handle, "epcgSetResource");
		epcgGetOptionList= (EPCGGetOptionList) dlsym (lib_handle, "epcgGetOptionList");
		epcgGetChoiceList= (EPCGGetChoiceList) dlsym (lib_handle, "epcgGetChoiceList");
		epcgSetPrintOption= (EPCGSetPrintOption) dlsym (lib_handle, "epcgSetPrintOption");
		epcgGetPageAttribute= (EPCGGetPageAttribute) dlsym (lib_handle, "epcgGetPageAttribute");
		epcgStartJob= (EPCGStartJob) dlsym (lib_handle, "epcgStartJob");
		epcgStartPage= (EPCGStartPage) dlsym (lib_handle, "epcgStartPage");
		epcgRasterOut= (EPCGRasterOut) dlsym (lib_handle, "epcgRasterOut");
		epcgEndPage= (EPCGEndPage) dlsym (lib_handle, "epcgEndPage");
		epcgEndJob= (EPCGEndJob) dlsym (lib_handle, "epcgEndJob");

		if (epcgInitialize == NULL
			|| epcgRelease == NULL
			|| epcgGetVersion == NULL
			|| epcgSetResource == NULL
			|| epcgGetOptionList == NULL
			|| epcgGetChoiceList == NULL
			|| epcgSetPrintOption == NULL
			|| epcgGetPageAttribute == NULL
			|| epcgStartJob == NULL
			|| epcgStartPage == NULL
			|| epcgRasterOut == NULL
			|| epcgEndPage == NULL
			|| epcgEndJob == NULL) {
			debuglog(("Failed to dlsym"));
			break;
		}

		resFunc.size = sizeof(EPS_RES_FUNC);
		resFunc.memAlloc = memAlloc;
		resFunc.memFree = memFree;
		resFunc.getLocalTime = getLocalTime;
		resFunc.resOpen = resOpen;
		resFunc.resRead = resRead;
		resFunc.resSeek = resSeek;
		resFunc.resClose= resClose;
		
		debuglog(("Model name : %s", PPD->modelname));

		error = epcgInitialize (PPD->modelname, &resFunc);
		if (error) {
			break;
		}

	} while (0);

	if(error && lib_handle) {
		dlclose (lib_handle);
		lib_handle = NULL;
	}

	*handle = lib_handle;
		
	debuglog(("TRACE OUT=%d", error));

	return error;
}

static int setup_option (void)
{
	debug_msg("%s:%d \t\t<<%s>>:\t\t Trace in\n", __FILE__, __LINE__, __FUNCTION__);	 

	EPS_INT32 optionCount = 0;
	EPS_INT8** optionList = NULL;
	char * option = NULL;
	char * choice = NULL;
	char choicetmp[20];
	

	ppd_attr_t * attr = NULL;
	int isFirst = 0;

	int error = 1;
	int i;

	char resource [PATH_MAX];
	
	do {
		isFirst = 1;
		while (attr = get_ppd_attr ("epcgResourceData", isFirst)) {
			memset(resource, 0x00, sizeof(resource));
			sprintf(resource, "%s/%s", CORE_RESOURCE_PATH, attr->value);

			error = epcgSetResource(atoi(attr->spec), resource);
			if (error) {
				break;
			}

			isFirst = 0;
		}
		
		if (error) {
			break;
		}

		error = epcgGetOptionList (&optionCount, NULL);
		if (error) {
			break;
		}
		
		optionList = (EPS_INT8**) eps_malloc (optionCount * sizeof(EPS_INT8*));
		if (optionList == NULL) {
			break;
		}

		error = epcgGetOptionList (&optionCount, optionList);
		if (error) {
			break;
		}

		debug_msg("%s:%d \t\t<<%s>>:\t\t Job Options =%s\n", __FILE__, __LINE__, __FUNCTION__, JobOptions);	 
		

		for (i = 0; i < optionCount; i++) {
			option = optionList[i];
			choice = get_option_for_job (option);
			if (choice == NULL) {
				choice = get_default_choice (option);
				if (choice == NULL) {
					error = 1;
					break;
				}
			}
			debug_msg("%s:%d \t\t<<%s>>:\t\t Option=%s Choice=%s\n", __FILE__, __LINE__, __FUNCTION__, option, choice);

			if(strcmp(option, "PageSize") == 0 && strcmp(choice, "Custom.595.28x841.89") == 0){
				CustomSize = 1;
			}

			if(strcmp(option, "ReduceEnlarge") == 0 && strcmp(choice, "ByOutputPaperSize") == 0){
				ReduceEnlag = 1;
			}

			if(strcmp(option, "ReduceEnlarge") == 0 && strcmp(choice, "ByPercentage") == 0 && CustomSize == 1){
				ReduceEnlagPer = 1;
			}

			if(ReduceEnlagPer == 1 && checkReduceEnlagPer == 0){
				strcpy(choicetmp, "ByOutputPaperSize");
				error = epcgSetPrintOption(option, choicetmp);
				checkReduceEnlagPer = 1;
			}else{
				error = epcgSetPrintOption(option, choice);
			}
			if (error) {
				break;
			}
		}

		if (error) {
			break;
		}
	
	} while (0);	

	if (optionList) {
		eps_free (optionList);
	}

	debug_msg("%s:%d \t\t<<%s>>:\t\t Trace out\n", __FILE__, __LINE__, __FUNCTION__);	 

	return error;
}

static int unload_core_library (HANDLE * handle)
{
	debuglog(("TRACE IN"));

	epcgRelease();
	
	if (handle) {
		dlclose(handle);
	}

	debuglog(("TRACE OUT=%d", 0));

	return 0;
}

static int print_page (void)
{
	debug_msg("%s:%d \t\t<<%s>>:\t\t Trace in\n", __FILE__, __LINE__, __FUNCTION__);

	cups_page_header_t header;

	EpsRasterPipeline * pipeline = NULL;
	char * image_raw = NULL;
	RASTER raster_h;

	EPS_INT32 printableWidth;
	EPS_INT32 printableHeight;
	EPS_INT32 flipVertical;
	EPS_INT32 flipHorizontal;
	EPS_BOOL bAbort;

	EpsPageInfo page = { 0 };
	EpsRasterOpt rasteropt;

	int error;
	size_t nraster;
	int i;
	EpsPageManager		*pageManager;
	EpsPageRegion		 pageRegion;
	EpsFilterPrintOption	filterPrintOption;

	rasteropt.drv_handle = NULL;
	rasteropt.raster_output = pipeOut;

	debug_msg("%s:%d \t\t<<%s>>:\t\t setup_filter_option\n", __FILE__, __LINE__, __FUNCTION__);
	error = setup_filter_option (&filterPrintOption);
	if(error) {
		error = 1;
		return error;
	}
	debug_msg("%s:%d \t\t<<%s>>:\t\t Start while cupsRasterReadHeader()\n", __FILE__, __LINE__, __FUNCTION__);

	int  page_count = 1;
	char ppmFileName[30];
	int check = 0;
	char choice_tmp[12];
	char option[10];
	int pageSize = 0;

	while (JobCanceled == 0 && error == 0 && cupsRasterReadHeader (Raster, &header)) {

		error = 0;
		raster_h = NULL;

		pageRegion.width = header.cupsWidth;
		pageRegion.height = header.cupsHeight;
		pageRegion.bytesPerLine = header.cupsBytesPerLine;
		pageRegion.bitsPerPixel = header.cupsBitsPerPixel;


		debug_msg("header.cupsWidth = %d\n", header.cupsWidth);
		debug_msg("header.cupsHeight = %d\n", header.cupsHeight);
		if(CustomSize == 1 && header.cupsBitsPerPixel == 8){
			if(check == 0){
				if(header.cupsWidth == 1356 && header.cupsHeight == 2076){
					strcpy(choice_tmp, "4x6in");
					strcpy(option, "PageSize");
					pageSize = 1;  /*4x6in*/
				}
				if(header.cupsWidth == 1716 && header.cupsHeight == 2438){
					strcpy(choice_tmp, "5x7in");
					strcpy(option, "PageSize");
					pageSize = 2;  /*5x7in*/
				}
				if(header.cupsWidth == 1177 && header.cupsHeight == 1716){
					strcpy(choice_tmp, "3.5x5in");
					strcpy(option, "PageSize");
					pageSize = 3;  /*3.5x5in*/
				}
				if(header.cupsWidth == 1716 && header.cupsHeight == 2796){
					strcpy(choice_tmp, "5x8in");
					strcpy(option, "PageSize");
					pageSize = 4;  /*5x8in*/
				}
				if(header.cupsWidth == 2796 && header.cupsHeight == 3516){
					strcpy(choice_tmp, "8x10in");
					strcpy(option, "PageSize");
					pageSize = 5;  /*8x10in*/
				}
				if(header.cupsWidth == 1356 && header.cupsHeight == 2475){
					strcpy(choice_tmp, "169widesize");
					strcpy(option, "PageSize");
					pageSize = 6;  /*169widesize*/
				}
				if(header.cupsWidth == 1404 && header.cupsHeight == 2013){
					strcpy(choice_tmp, "100x148mm");
					strcpy(option, "PageSize");
					pageSize = 7;  /*100x148mm*/
				}
				if(header.cupsWidth == 1160 && header.cupsHeight == 3336){
					strcpy(choice_tmp, "ENVELOPE_10");
					strcpy(option, "PageSize");
					pageSize = 8;  /*ENVELOPE_10*/
				}
				if(header.cupsWidth == 1234 && header.cupsHeight == 3034){
					strcpy(choice_tmp, "ENVELOPEDL");
					strcpy(option, "PageSize");
					pageSize = 9;  /*ENVELOPEDL*/
				}
				if(header.cupsWidth == 1290 && header.cupsHeight == 2212){
					strcpy(choice_tmp, "ENVELOPEC6");
					strcpy(option, "PageSize");
					pageSize = 10;  /*ENVELOPEC6*/
				}
				if(header.cupsWidth == 4579 && header.cupsHeight == 6761){
					strcpy(choice_tmp, "A3+");
					strcpy(option, "PageSize");
					pageSize = 11;  /*A3+*/
				}
				if(pageSize != 0)
					epcgSetPrintOption(option, choice_tmp);
				check = 1;
			}
		}
		//PPM header file
		/*
		sprintf(ppmFileName, "/tmp/test%d.ppm", page_count);
		{
			FILE *fp;
			fp=fopen(ppmFileName, "w");
			fprintf(fp, "P3\n");
			fprintf(fp, "%d\n",header.cupsBytesPerLine/3);
			fprintf(fp, "%d\n",header.cupsHeight);
			fprintf(fp, "255\n");
			fclose(fp);
		} */

		page_count++;

		debug_msg("%s:%d \t\t<<%s>>:\t\t Create PageManager: \n", __FILE__, __LINE__, __FUNCTION__);
		pageManager = pageManagerCreate(pageRegion, filterPrintOption, rasterSource);

		debug_msg("%s:%d \t\t<<%s>>:\t\t PageRegion: \n", __FILE__, __LINE__, __FUNCTION__);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t width = %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.width);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t height = %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.height);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t bytesPerLine = %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.bytesPerLine);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t bitsPerPixel = %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.bitsPerPixel);
		if (pageManager == NULL) {
			error = 1;
			break;
		}
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t pageRegion.width =  %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.bytesPerLine, pageRegion.width);
		pageManagerGetPageRegion(pageManager, &pageRegion);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t pageRegion.width =  %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.bytesPerLine, pageRegion.width);
		
		image_raw = (char * ) eps_malloc(pageRegion.bytesPerLine);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t imageraw_size =  %d: \n", __FILE__, __LINE__, __FUNCTION__, pageRegion.bytesPerLine);
		if (image_raw == NULL) {
			error = 1;
			break;
		}
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t start getPageAttribute\n", __FILE__, __LINE__, __FUNCTION__);
		epcgGetPageAttribute (EPS_PAGEATTRIB_PRINTABLEAREA_WIDTH, &printableWidth);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t printableWidth =  %d: \n", __FILE__, __LINE__, __FUNCTION__, printableWidth);
		epcgGetPageAttribute (EPS_PAGEATTRIB_PRINTABLEAREA_HEIGHT, &printableHeight);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t printableHeight =  %d: \n", __FILE__, __LINE__, __FUNCTION__, printableHeight);

		if(CustomSize == 1 && header.cupsBitsPerPixel == 8 && ReduceEnlag == 0 && ReduceEnlagPer == 0){
			if(pageSize == 1){
				printableWidth = 1356;
				printableHeight = 2076;
			}
			if(pageSize == 2){
				printableWidth = 1716;
				printableHeight = 2438;
			}
			if(pageSize == 3){
				printableWidth = 1177;
				printableHeight = 1176;
			}
			if(pageSize == 4){
				printableWidth = 1716;
				printableHeight = 2796;
			}
			if(pageSize == 5){
				printableWidth = 2796;
				printableHeight = 3516;
			}
			if(pageSize == 6){
				printableWidth = 1356;
				printableHeight = 2476;
			}
			if(pageSize == 7){
				printableWidth = 1333;
				printableHeight = 2014;
			}
			if(pageSize == 8){
				printableWidth = 1401;
				printableHeight = 3095;
			}
			if(pageSize == 9){
				printableWidth = 1475;
				printableHeight = 2793;
			}
			if(pageSize == 10){
				printableWidth = 1532;
				printableHeight = 1971;
			}
			if(pageSize == 11){
				printableWidth = 4663;
				printableHeight = 6846;
			}
			
		}

		if(CustomSize == 1 && (ReduceEnlag == 1 || ReduceEnlagPer == 1)){
			printableWidth = 1356;
			printableHeight = 2076;
		}

		epcgGetPageAttribute (EPS_PAGEATTRIB_FLIP_VERTICAL, &flipVertical);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t flipVertical =  %d: \n", __FILE__, __LINE__, __FUNCTION__, flipVertical);
		epcgGetPageAttribute (EPS_PAGEATTRIB_FLIP_HORIZONTAL, &flipHorizontal);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t flipHorizontal =  %d: \n", __FILE__, __LINE__, __FUNCTION__, flipHorizontal);
		
		page.bytes_per_pixel = pageRegion.bitsPerPixel / 8;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t bytes_per_pixel =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.bytes_per_pixel);
		page.src_print_area_x = pageRegion.width;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.src_print_area_x =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.src_print_area_x);
		page.src_print_area_y = pageRegion.height; 
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.src_print_area_y =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.src_print_area_y);

	
		page.prt_print_area_x = printableWidth;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.prt_print_area_x =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.prt_print_area_x);
		page.prt_print_area_y = printableHeight;

		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.prt_print_area_y =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.prt_print_area_y);
		page.reverse = (flipVertical) ? 1 : 0;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.reverse =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.reverse);
		page.mirror = (flipHorizontal) ? 1 : 0;
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.mirror =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.mirror);
		page.scale = ((page.src_print_area_x != page.prt_print_area_x) || (page.src_print_area_y != page.prt_print_area_y)) ? 1 : 0;
		debug_msg("page.scale = %d", page.scale);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t page.scale =  %d: \n", __FILE__, __LINE__, __FUNCTION__, page.scale);
		do {
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t raster_helper_create_pipeline() \n", __FILE__, __LINE__, __FUNCTION__);
			pipeline = (EpsRasterPipeline *) raster_helper_create_pipeline(&page, EPS_RASTER_PROCESS_MODE_PRINTING);	
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t eps_raster_init() \n", __FILE__, __LINE__, __FUNCTION__);
			if (eps_raster_init(&raster_h, &rasteropt, pipeline)) {
				error = 1;
				break;
			}
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t epcgStartPage() \n", __FILE__, __LINE__, __FUNCTION__);
			if (epcgStartPage()) {
				debug_msg("%s:%d \t\t<<%s>>:\t\t\t abort -> endPage() \n", __FILE__, __LINE__, __FUNCTION__);
				epcgEndPage(TRUE);  /* Abort */
				error = 1;
				break;
			}
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t start getPageRegion raw() \n", __FILE__, __LINE__, __FUNCTION__);
			for (i = 0; i < pageRegion.height; i++) {
				debug_msg("%s:%d \t\t<<%s>>:\t\t\t getraster line[%d]() \n", __FILE__, __LINE__, __FUNCTION__, i);
				if ((pageManagerGetRaster(pageManager, image_raw, pageRegion.bytesPerLine) != EPS_OK) || (JobCanceled)) {
					debug_msg("%s:%d \t\t<<%s>>:\t\t\t getraster fail[%d]() \n", __FILE__, __LINE__, __FUNCTION__, i);
					error = 1;
					break;
				}
				///Add for print PPM file
				/*
				{
					FILE *fp;
					fp=fopen(ppmFileName, "a+");
					int j=0;	
					for(j=0; j<pageRegion.bytesPerLine; j++){		
						fprintf(fp, "%u ", (unsigned char)image_raw[j]);
					}
					fprintf(fp, "\n");
					fclose(fp);
				} */
						
				debug_msg("%s:%d \t\t<<%s>>:\t\t\t eps_raster_print() \n", __FILE__, __LINE__, __FUNCTION__, i);
				if (eps_raster_print(raster_h, image_raw, pageRegion.bytesPerLine, pageRegion.width, (int *)&nraster)) {
					debug_msg("%s:%d \t\t<<%s>>:\t\t\t print raster fail[%d]() \n", __FILE__, __LINE__, __FUNCTION__, i);
					error  = 1;
					break;
				}
			}

			// flushing page
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t flusing line[%d]() \n", __FILE__, __LINE__, __FUNCTION__, i);
			eps_raster_print(raster_h, NULL, 0, 0, (int *)&nraster);

			bAbort = (error) ? TRUE : FALSE;
			if (epcgEndPage (bAbort)) {
				debuglog(("bAbort"));
				error = 1;
			}

#if (0)
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t page_no = %d, pageHeight = %d", __FILE__, __LINE__, __FUNCTION__, ++page_no, pageHeight);
#endif
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t Safe free raster_h\n", __FILE__, __LINE__, __FUNCTION__);
			safeFree(raster_h, eps_raster_free);
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t Safe free pipeline\n", __FILE__, __LINE__, __FUNCTION__);
			safeFree(pipeline, raster_helper_destroy_pipeline);
			debug_msg("%s:%d \t\t<<%s>>:\t\t\t end one while raster read\n", __FILE__, __LINE__, __FUNCTION__);
		if (i == 1715) 
			{
				debug_msg("%s:%d \t\t<<%s>>:\t\t\t break\n", __FILE__, __LINE__, __FUNCTION__);
				break;
			}
		} while (error == 0 && pageManagerIsNextPage(pageManager) == TRUE);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t end while\n", __FILE__, __LINE__, __FUNCTION__);
		safeFree(image_raw, eps_free);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free raster_h\n", __FILE__, __LINE__, __FUNCTION__);
		safeFree(raster_h, eps_raster_free);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free pipeline\n", __FILE__, __LINE__, __FUNCTION__);
		safeFree(pipeline, raster_helper_destroy_pipeline);
		debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free pageManage\n", __FILE__, __LINE__, __FUNCTION__);
		safeFree(pageManager, pageManagerDestroy);
		debug_msg(("Apptemp end while raster read\n"));
	}
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free image_raw\n", __FILE__, __LINE__, __FUNCTION__);
	safeFree(image_raw, eps_free);
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free raster_h\n", __FILE__, __LINE__, __FUNCTION__);
	safeFree(raster_h, eps_raster_free);
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free pipeline\n", __FILE__, __LINE__, __FUNCTION__);
	safeFree(pipeline, raster_helper_destroy_pipeline);
	debug_msg("%s:%d \t\t<<%s>>:\t\t\t safe free pageManager\n", __FILE__, __LINE__, __FUNCTION__);
	safeFree(pageManager, pageManagerDestroy);

	debuglog(("TRACE OUT=%d", error));

	return error;
}

int printJob (void)
{
	debug_msg("%s:%d \t\t<<%s>>:\t\t Trace in\n", __FILE__, __LINE__, __FUNCTION__);

	HANDLE lib_handle = NULL;
	EPS_BOOL jobStarted = FALSE;
	int error = 1; 

	do {
		debug_msg("%s:%d \t\t<<%s>>:\t\t load core library\n", __FILE__, __LINE__, __FUNCTION__);
		error = load_core_library (&lib_handle);
		if(error) {
			break;
		}
		debug_msg("%s:%d \t\t<<%s>>:\t\t setup option\n", __FILE__, __LINE__, __FUNCTION__);
		error = setup_option ();
		if(error) {
			break;
		}

		debug_msg("%s:%d \t\t<<%s>>:\t\t Job name = %s\n", __FILE__, __LINE__, __FUNCTION__, JobName);

		
		debug_msg("%s:%d \t\t<<%s>>:\t\t epcgStartJob()\n", __FILE__, __LINE__, __FUNCTION__);
		error = epcgStartJob((EPS_PrintStream) printStream, JobName);
		if(error) {
			break;
		}

		jobStarted = TRUE;

		debug_msg("%s:%d \t\t<<%s>>:\t\t print_page()\n", __FILE__, __LINE__, __FUNCTION__);
		error = print_page ();
		if(error) {
			debuglog(("break after endpage"));
			break;
		}

		error = 0;

	} while (0);
	if (jobStarted == TRUE) {
		debug_msg("%s:%d \t\t<<%s>>:\t\t call end job\n", __FILE__, __LINE__, __FUNCTION__);
		epcgEndJob();
		debug_msg("%s:%d \t\t<<%s>>:\t\t end job called\n", __FILE__, __LINE__, __FUNCTION__);
	}
	debug_msg("%s:%d \t\t<<%s>>:\t\t Unload library\n", __FILE__, __LINE__, __FUNCTION__);
	if (lib_handle) {
		debug_msg("%s:%d \t\t<<%s>>:\t\t Unload core library\n", __FILE__, __LINE__, __FUNCTION__);
		unload_core_library (lib_handle);
		debug_msg("%s:%d \t\t<<%s>>:\t\t Unload core library completed\n", __FILE__, __LINE__, __FUNCTION__);
	}
	debug_msg("%s:%d \t\t<<%s>>:\t\t Traceout, err = %d\n", __FILE__, __LINE__, __FUNCTION__, error);

	return error;
}
