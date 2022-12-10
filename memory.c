#include "minic.h"

// 128 MiB
#define TOTAL_MEMORY_SIZE (128 * 1024 * 1024)
// 16 MiB
#define TEMP_MEMORY_SIZE (16 * 1024 * 1024)
#define GENERAL_MEMORY_SIZE (TOTAL_MEMORY_SIZE - TEMP_MEMORY_SIZE)

memory initMemory(void)
{
	u8 *ptr = mmap(NULL, TOTAL_MEMORY_SIZE, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	memory mem = {
		.temp = createBump(ptr, TEMP_MEMORY_SIZE),
		.general =
			createBump(ptr + TEMP_MEMORY_SIZE, GENERAL_MEMORY_SIZE),
	};
	return mem;
}
