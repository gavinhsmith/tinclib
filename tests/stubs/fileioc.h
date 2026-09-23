/* Stand-in for CEdev's fileioc.h: one in-memory appvar. See stubs.c. */
#ifndef FILEIOC_H
#define FILEIOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint8_t ti_Open(const char *name, const char *mode);
int ti_Close(uint8_t handle);
size_t ti_Write(const void *data, size_t size, size_t count, uint8_t handle);
size_t ti_Read(void *data, size_t size, size_t count, uint8_t handle);
int ti_Seek(int offset, unsigned int origin, uint8_t handle);
int ti_Delete(const char *name);

#endif
