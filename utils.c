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

void debugLog(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	debugLogV(fmt, ap);
	va_end(ap);
}

void debugLogV(char *fmt, va_list ap)
{
	fprintf(stderr, "\033[35mlog:\033[0;1m ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\033[0m\n");
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

u64 rotl(u64 value, u64 count)
{
	u64 mask = 8 * sizeof(value) - 1;
	count &= mask;
	return (value << count) | (value >> (-count & mask));
}

u64 rotr(u64 value, u64 count)
{
	u64 mask = 8 * sizeof(value) - 1;
	count &= mask;
	return (value >> count) | (value << (-count & mask));
}

u64 fxhash(u8 *ptr, usize len)
{
	u64 hash = 0;
	for (usize i = 0; i < len; i++)
		hash = (rotl(hash, 5) ^ ptr[i]) * 0x517cc1b727220a95;
	return hash;
}
