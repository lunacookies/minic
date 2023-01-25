#include "minic.h"

void internalError(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	internalErrorV(fmt, ap);
	// no need for va_end(ap);
	// because we’ve already aborted by this point
}

void internalErrorV(char *fmt, va_list ap)
{
	fprintf(stderr, "\033[31minternal error:\033[0;1m ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\033[0m\n");

	abort();
}

u32 numCpus()
{
	const char *name = "hw.logicalcpu";

	// first we do a sanity check by asking the OS
	// how many bytes are needed to store the number of CPUs
	usize desired_size = 0;
	sysctlbyname(name, NULL, &desired_size, NULL, 0);
	assert(desired_size == 4);

	u32 num_cpus = 0;
	size_t size = 4;
	sysctlbyname(name, &num_cpus, &size, NULL, 0);

	// size now contains the number of bytes filled;
	// all 4 bytes of “num_cpus” should’ve been filled
	assert(size == 4);

	return num_cpus;
}
