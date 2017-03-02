/*
 * @brief U-Storage Project
 *
 * @note
 * Copyright(C) i4season, 2016
 * Copyright(C) Szitman, 2016
 * All rights reserved.
 */
#include <stdint.h>
#include <stdio.h>

void log_debug(FILE *fp, char *fname, const char *func, int lineno, char *fmt, ...)
{

	va_list ap;
	pid_t pid;
	time_t t;
	struct tm *tm, tmptm={0};
	
	pid = getpid();

	t = time(NULL);
	localtime_r(&t, &tmptm);
	tm=&tmptm;
	fprintf(fp, "[%04d/%02d/%02d %02d:%02d:%02d] ",
					tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
					tm->tm_hour, tm->tm_min, tm->tm_sec);
	fprintf(fp, "[pid:%d] ", pid);

	fprintf(fp, "(%s:%s():%d) ", fname, func, lineno);

	va_start(ap, fmt);
	if (vfprintf(fp, fmt, ap) == -1){
		va_end(ap);
		return;
	}
	
	va_end(ap);
	fflush(fp);
}

