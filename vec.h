#ifndef MINIC_VEC_H
#define MINIC_VEC_H

#include <stddef.h>

struct vec {
	char *ptr;
	size_t len;
	size_t cap;
	size_t elemsize;
};

struct vec
vec_new(size_t elemsize);

void
vec_push(struct vec *v, void *elem);

#endif
