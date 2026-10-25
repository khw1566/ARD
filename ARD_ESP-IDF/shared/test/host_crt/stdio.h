#pragma once
#include <stddef.h>
typedef struct _iobuf FILE;
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fputc(int, FILE *);
int _snprintf(char *, size_t, const char *, ...);
#define snprintf _snprintf
int puts(const char *);
