#include "minic.h"

void internalError(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "\033[31minternal error:\033[0;1m ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\033[0m\n");
	abort();
}
