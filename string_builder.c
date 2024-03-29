#include "minic.h"

stringBuilder stringBuilderCreate(bump *b)
{
	return (stringBuilder){
		.bump = b,
		.s = (char *)(b->top + b->bytes_used),
		.previous_bytes_used = b->bytes_used,
	};
}

void stringBuilderPrintf(stringBuilder *sb, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(sb, fmt, ap);
	va_end(ap);
}

void stringBuilderPrintfV(stringBuilder *sb, const char *fmt, va_list ap)
{
	assert(sb->bump->bytes_used == sb->previous_bytes_used);

	usize remaining_bytes = sb->bump->max_size - sb->bump->bytes_used;

	char *p = (char *)(sb->bump->top + sb->bump->bytes_used);
	usize length = vsnprintf(p, remaining_bytes, fmt, ap);
	assert(length < remaining_bytes);

	// The length returned by vsnprintf does not include
	// the null terminator.
	sb->bump->bytes_used += length;
	sb->previous_bytes_used = sb->bump->bytes_used;
}

char *stringBuilderFinish(stringBuilder sb)
{
	assert(sb.bump->bytes_used == sb.previous_bytes_used);
	sb.bump->top[sb.bump->bytes_used] = 0;
	sb.bump->bytes_used++;
	return sb.s;
}
