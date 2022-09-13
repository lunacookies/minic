#include "bump.h"

#include <stdio.h>
#include <stdlib.h>

#define CHUNKSIZE 2048

struct bump
bump_new(void)
{
	char *head = malloc(CHUNKSIZE);
	struct bump b = {.head = head, .end = head + CHUNKSIZE};
	return b;
}

void *
bump_alloc(struct bump *b, size_t bytes)
{
	if (b->head == b->end) {
		b->head = malloc(CHUNKSIZE);
		b->end = b->head + CHUNKSIZE;
	}
	void *ptr = b->head;
	b->head += bytes;
	return ptr;
}
