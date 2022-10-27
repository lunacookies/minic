#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void
error(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "\033[1;31merror:\033[0m ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	exit(1);
}
