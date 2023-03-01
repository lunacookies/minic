#include "minic.h"

enum {
	TEMP_MEMORY_SIZE = 16 * 1024 * 1024,
	GENERAL_MEMORY_SIZE = 96 * 1024 * 1024,
};

bump allocateFromOs(usize size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return bumpCreate(p, size);
}

memory memoryCreate(void)
{
	return (memory){
		.temp = allocateFromOs(TEMP_MEMORY_SIZE),
		.general = allocateFromOs(GENERAL_MEMORY_SIZE),
	};
}
