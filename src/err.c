/*
 * EPSON ESC/P-R Printer Driver for Linux
 * Copyright (C) 2002-2005 AVASYS CORPORATION.
 * Copyright (C) Seiko Epson Corporation 2002-2005.
 *
 *  This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA.
 *
 * As a special exception, AVASYS CORPORATION gives permission to
 * link the code of this program with libraries which are covered by
 * the AVASYS Public License and distribute their linked
 * combinations.  You must obey the GNU General Public License in all
 * respects for all of the code used other than the libraries which
 * are covered by AVASYS Public License.
 */
#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "err.h"
#include "time.h"

#define HAVE_DEBUG 0

/* global  */
static char err_pname[256] = "";
static FILE *debug_f = NULL;
static FILE *debug_f2 = NULL;

/* static functions */
static void err_doit (enum msgtype, int, const char *, va_list);


void debug_msg(const char *fmt, ...){
#if (HAVE_DEBUG)	
	va_list ap;
	
	if(!debug_f){
		debug_f = fopen(DEBUG_PATH, "wb");
		if(debug_f == NULL){
			return;
		}
		fchmod (fileno (debug_f), 0777);
	}
	
	va_start (ap, fmt);
	vfprintf (debug_f, fmt, ap);
	fflush(debug_f);
	va_end (ap);
#endif
	return;
}

void debugt_msg(char *  fmt, ...)
{
#if (HAVE_DEBUG_T)
	if(fmt == NULL || strlen(fmt) == 0){
		return;
	}
	
   va_list ap;
	if(!debug_f2){
		debug_f2 = fopen(EPS_DEBUG_PATH2, "wb");
		if(debug_f2 == NULL){
			return;
		}
		fchmod (fileno (debug_f2), 0644);
	}
	
	time_t ltime; /* calendar time */
	struct tm *Tm;
	struct timespec start;
	char szBuf[256];
	char *p = szBuf;
	int err = 0;
	
	ltime=time(NULL); /* get current cal time */
	Tm = localtime(&ltime);
   err = clock_gettime(CLOCK_REALTIME, &start);
   if(err == 0){
   	sprintf(szBuf, "[%02d-%02d-%02d %02d:%02d:%02d::%02d] %s", Tm->tm_year % 10, Tm->tm_mon + 1, Tm->tm_mday, Tm->tm_hour, Tm->tm_min, Tm->tm_sec, (int)start.tv_nsec/1000000, fmt);
   }else{
   	sprintf(szBuf, "[%02d-%02d-%02d %02d:%02d:%02d] %s", Tm->tm_year % 10, Tm->tm_mon + 1, Tm->tm_mday, Tm->tm_hour, Tm->tm_min, Tm->tm_sec, fmt);
   }
	
	va_start (ap, szBuf);
	vfprintf (debug_f2, szBuf, ap);
	fflush(debug_f2);
	va_end (ap);
#endif
	return 0;
}

void
err_init (const char *name)
{
	if (name && strlen (name) < 256)
		strcpy (err_pname, name);
	return;
}

void
err_msg (enum msgtype type, const char *fmt, ...)
{
	va_list ap;

	va_start (ap, fmt);
	err_doit (type, 0, fmt, ap);

	va_end (ap);
	return;
}

void
err_fatal (const char *fmt, ...)
{
	va_list ap;

	va_start (ap, fmt);
	err_doit (MSGTYPE_ERROR, 0, fmt, ap);

	va_end (ap);
	exit (1);
}

void
err_system (const char *fmt,...)
{
	int e;
	va_list ap;

	e = errno;
	va_start (ap, fmt);
	err_doit (MSGTYPE_ERROR, e, fmt, ap);

	va_end (ap);
	exit (1);
}

static void
err_doit (enum msgtype type, int e, const char *fmt, va_list ap)
{
	if (err_pname[0] != '\0')
		fprintf (stderr, "%s : ", err_pname);

	if (type == MSGTYPE_ERROR)
		fprintf (stderr, "**** ERROR **** : ");
	else if (type == MSGTYPE_WARNING)
		fprintf (stderr, "**** WARNING **** : ");
	else if (type == MSGTYPE_INFO)
		fprintf (stderr, "**** INFO **** : ");
		
	vfprintf (stderr, fmt, ap);

	if (e)
		fprintf (stderr, " : %s", strerror (e));
	
	fprintf (stderr, "\n");
	fflush (stderr);
	return;
}
