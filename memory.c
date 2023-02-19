#include "minic.h"

#define MiB (1024 * 1024)
#define TEMP_MEMORY_SIZE (16 * MiB)
#define GENERAL_MEMORY_SIZE (96 * MiB)

bump allocateFromOs(usize size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return createBump(p, size);
}

memory initMemory(void)
{
	return (memory){
		.temp = allocateFromOs(TEMP_MEMORY_SIZE),
		.general = allocateFromOs(GENERAL_MEMORY_SIZE),
	};
}
