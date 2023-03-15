#include "minic.h"

enum { UNINIT_SENTINEL = '`' };

static void *bumpAllocateInternal(bump *b, usize size)
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

bump bumpCreate(void *buffer, usize size)
{
	return (bump){
		.top = buffer,
		.bytes_used = 0,
		.max_size = size,
		.array_builder_nesting_level = 0,
	};
}

bumpMark bumpCreateMark(bump *b)
{
	return (bumpMark){
		.bytes_used = b->bytes_used,
		.top = b->top,
	};
}

void bumpClearToMark(bump *b, bumpMark mark)
{
	assert(b->top == mark.top);
	assert(b->bytes_used >= mark.bytes_used);

	usize bytes_allocated_since_mark = b->bytes_used - mark.bytes_used;
	memset(b->top + mark.bytes_used, UNINIT_SENTINEL,
	       bytes_allocated_since_mark);
	b->bytes_used = mark.bytes_used;
}

void *bumpAllocate(bump *b, usize size)
{
	assert(b->array_builder_nesting_level == 0);
	return bumpAllocateInternal(b, size);
}

void *bumpCopy(bump *b, void *buffer, usize size)
{
	assert(b->array_builder_nesting_level == 0);
	void *ptr = bumpAllocate(b, size);
	memcpy(ptr, buffer, size);
	return ptr;
}

bump bumpCreateSubBump(bump *b, usize size)
{
	assert(b->array_builder_nesting_level == 0);
	void *buffer = bumpAllocate(b, size);
	return bumpCreate(buffer, size);
}

u8 *bumpPrintf(bump *b, char *fmt, ...)
{
	assert(b->array_builder_nesting_level == 0);
	va_list ap;
	va_start(ap, fmt);
	u8 *p = bumpPrintfV(b, fmt, ap);
	va_end(ap);
	return p;
}

u8 *bumpPrintfV(bump *b, char *fmt, va_list ap)
{
	assert(b->array_builder_nesting_level == 0);

	usize remaining_bytes = b->max_size - b->bytes_used;

	u8 *p = b->top + b->bytes_used;
	usize length = vsnprintf((char *)p, remaining_bytes, fmt, ap);

	// The length returned by vsnprintf does not include
	// the null terminator.
	length++;

	assert(length < remaining_bytes);
	b->bytes_used += length;
	return p;
}

arrayBuilder bumpStartArrayBuilder(bump *b, usize element_size)
{
	b->array_builder_nesting_level++;
	return (arrayBuilder){
		.b = b,
		.top = b->top + b->bytes_used,
		.element_size = element_size,
	};
}

void arrayBuilderPush(arrayBuilder *ab, void *element)
{
	void *ptr = bumpAllocateInternal(ab->b, ab->element_size);
	memcpy(ptr, element, ab->element_size);
}

void *bumpFinishArrayBuilder(bump *b, arrayBuilder *ab)
{
	assert(b == ab->b);
	b->array_builder_nesting_level--;

	// Zero out arrayBuilder fields to prevent accidental uses afterwards.
	void *p = ab->top;
	*ab = (arrayBuilder){ 0 };

	return p;
}
