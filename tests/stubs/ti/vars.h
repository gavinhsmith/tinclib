/* Stand-in for CEdev's ti/vars.h: os_RunPrgm only. See stubs.c. */
#ifndef TI_VARS_H
#define TI_VARS_H

#include <stddef.h>

typedef int (*os_runprgm_callback_t)(void *data, int retval);
int os_RunPrgm(const char *prgm, void *data, size_t size, os_runprgm_callback_t callback);

#endif
