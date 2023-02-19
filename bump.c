#include "minic.h"

#define UNINIT_SENTINEL '`'

bump createBump(void *buffer, usize size)
{
	return (bump){
		.top = buffer,
		.bytes_used = 0,
		.max_size = size,
	};
}

bumpMark markBump(bump *b)
{
	return (bumpMark){
		.bytes_used = b->bytes_used,
		.top = b->top,
	};
}

void clearBumpToMark(bump *b, bumpMark mark)
{
	assert(b->top == mark.top);
	assert(b->bytes_used >= mark.bytes_used);

	usize bytes_allocated_since_mark = b->bytes_used - mark.bytes_used;
	memset(b->top + mark.bytes_used, UNINIT_SENTINEL,
	       bytes_allocated_since_mark);
	b->bytes_used = mark.bytes_used;
}

void *allocateInBump(bump *b, usize size)
{
	if (b->bytes_used + size > b->max_size)
		internalError("out of memory\n%zu KiB attempted\n"
			      "%zu KiB used\n%zu KB remaining\n%zu KB total "
			      "space",
			      size / 1024, b->bytes_used / 1024,
			      (b->max_size - b->bytes_used) / 1024,
			      b->max_size / 1024);

	void *ptr = b->top + b->bytes_used;
	b->bytes_used += size;
	memset(ptr, UNINIT_SENTINEL, size);
	return ptr;
}

void *copyInBump(bump *b, void *buffer, usize size)
{
	void *ptr = allocateInBump(b, size);
	memcpy(ptr, buffer, size);
	return ptr;
}

void printfInBump(bump *b, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printfInBumpV(b, fmt, ap);
	va_end(ap);
}

void printfInBumpV(bump *b, char *fmt, va_list ap)
{
	usize remaining_bytes = b->max_size - b->bytes_used;
	usize length = vsnprintf((char *)(b->top + b->bytes_used),
				 remaining_bytes, fmt, ap);
	assert(length < remaining_bytes);
	b->bytes_used += length;
}
