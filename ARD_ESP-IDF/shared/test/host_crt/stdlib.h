#pragma once
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t,size_t);
void *realloc(void *,size_t);
void free(void *);
int abs(int);
_Noreturn void _exit(int);
long labs(long);
