#include "minic.h"

enum { UNINIT_SENTINEL = '`' };

static void *bumpConsumeSpace(bump *b, usize size)
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
	return ptr;
}

static void bumpAlignTo(bump *b, usize alignment)
{
	assert(b->array_builder_nesting_level == 0);

	usize padding_size =
		alignment - ((usize)(b->top + b->bytes_used) % alignment);
	b->padding_bytes_used += padding_size;

	void *padding = bumpConsumeSpace(b, padding_size);

	// We’re pedantic, and mark padding as uninitialized.
	memset(padding, UNINIT_SENTINEL, padding_size);

	assert((usize)(b->top + b->bytes_used) % alignment == 0);
}

static void *bumpAllocate(bump *b, usize size, usize alignment)
{
	assert(b->array_builder_nesting_level == 0);
	bumpAlignTo(b, alignment);
	void *ptr = bumpConsumeSpace(b, size);
	memset(ptr, UNINIT_SENTINEL, size);
	return ptr;
}

bump bumpCreate(void *buffer, usize size)
{
	return (bump){
		.top = buffer,
		.bytes_used = 0,
		.max_size = size,
		.padding_bytes_used = 0,
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

void *bumpAllocateArray_(bump *b, usize count, usize element_size)
{
	assert(b->array_builder_nesting_level == 0);

	// We align to the element size to minimize chances that,
	// when the array we’re currently allocating is later accessed,
	// an element straddles a cache line boundary (which is bad for perf).
	usize alignment = element_size;

	return bumpAllocate(b, element_size * count, alignment);
}

void *bumpCopyArray_(bump *b, void *buffer, usize count, usize element_size)
{
	assert(b->array_builder_nesting_level == 0);
	void *ptr = bumpAllocateArray_(b, count, element_size);
	memcpy(ptr, buffer, element_size * count);
	return ptr;
}

bump bumpCreateSubBump(bump *b, usize size)
{
	assert(b->array_builder_nesting_level == 0);

	// We default to an alignment of 16, like malloc.
	void *buffer = bumpAllocate(b, size, 16);

	return bumpCreate(buffer, size);
}

char *bumpPrintf(bump *b, const char *fmt, ...)
{
	assert(b->array_builder_nesting_level == 0);
	va_list ap;
	va_start(ap, fmt);
	char *p = bumpPrintfV(b, fmt, ap);
	va_end(ap);
	return p;
}

char *bumpPrintfV(bump *b, const char *fmt, va_list ap)
{
	assert(b->array_builder_nesting_level == 0);

	usize remaining_bytes = b->max_size - b->bytes_used;

	char *p = (char *)(b->top + b->bytes_used);
	usize length = vsnprintf(p, remaining_bytes, fmt, ap);

	// The length returned by vsnprintf does not include
	// the null terminator.
	length++;

	bumpConsumeSpace(b, length);
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
	void *ptr = bumpConsumeSpace(ab->b, ab->element_size);
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
