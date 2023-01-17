#include "minic.h"

// We take char instead of u8 to allow for the passing of unadorned string
// literals.
void error(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	fprintf(stderr, "\033[1;31minternal error:\033[0m ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	abort();
}
