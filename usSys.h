#ifndef __USSYS_H_
#define __USSYS_H_

#include <stdint.h>
#include <ctype.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern void log_debug(FILE *fp, char *fname, const char *func, int lineno, char *fmt, ...);

#define DEBUG(fmt, arg...)	do{log_debug(stderr, __FILE__, __FUNCTION__ ,  __LINE__, fmt, ##arg); } while (0)


#ifdef __cplusplus
}
#endif
#endif

