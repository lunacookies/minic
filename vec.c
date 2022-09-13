#include "vec.h"

#include <stdlib.h>
#include <string.h>

struct vec
vec_new(size_t elemsize)
{
	struct vec v = {.ptr = malloc(8 * elemsize),
			.len = 0,
			.cap = 8,
			.elemsize = elemsize};
	return v;
}

void
vec_push(struct vec *v, void *elem)
{
	if (v->len == v->cap) {
		v->cap *= 2;
		v->ptr = realloc(v->ptr, v->cap * v->elemsize);
	}
	memcpy(v->ptr + v->len * v->elemsize, elem, v->elemsize);
	v->len += 1;
}
