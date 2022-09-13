#ifndef MINIC_BUMP_H
#define MINIC_BUMP_H

#include <stddef.h>

struct bump {
	char *head;
	char *end;
};

struct bump
bump_new(void);

void *
bump_alloc(struct bump *b, size_t bytes);

#endif
