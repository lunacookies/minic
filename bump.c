#include "minic.h"

bump createBump(void *buffer, usize size)
{
	bump b = {
		.top = buffer,
		.bytes_used = 0,
		.max_size = size,
	};
	return b;
}

void clearBump(bump *b)
{
	// Zero out everything we’ve used so far.
	memset(b->top, 0, b->bytes_used);
	b->bytes_used = 0;
}

void *allocateInBump(bump *b, usize size)
{
	void *ptr = b->top + b->bytes_used;
	if (b->bytes_used + size > b->max_size)
		error("out of memory\n%zu KiB attempted\n"
		      "%zu KiB used\n%zu KB remaining\n%zu KB total "
		      "space",
		      size / 1024, b->bytes_used / 1024,
		      (b->max_size - b->bytes_used) / 1024, b->max_size / 1024);
	b->bytes_used += size;
	return ptr;
}
