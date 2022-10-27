#include "minic.h"

// We take char instead of u8 to allow for
// the passing of unadorned string literals.
void
Error(char *Fmt, ...)
{
	va_list Ap;
	va_start(Ap, Fmt);
	fprintf(stderr, "\033[1;31merror:\033[0m ");
	vfprintf(stderr, Fmt, Ap);
	fprintf(stderr, "\n");
	exit(1);
}
