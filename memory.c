#include "minic.h"

// 128 MiB
#define TOTAL_MEMORY_SIZE (128 * 1024 * 1024)
// 16 MiB
#define TEMP_MEMORY_SIZE (16 * 1024 * 1024)
#define GENERAL_MEMORY_SIZE (TOTAL_MEMORY_SIZE - TEMP_MEMORY_SIZE)

static u8 *allocFromOs(usize size, u8 *desired_addr)
{
	void *ptr = mmap(desired_addr, size, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert(ptr == desired_addr);
	return ptr;
}

memory initMemory(void)
{
	u8 *temp_ptr = allocFromOs(TEMP_MEMORY_SIZE, (u8 *)0x0000200000000000);
	u8 *general_ptr =
		allocFromOs(GENERAL_MEMORY_SIZE, (u8 *)0x0000400000000000);

	memory mem = {
		.temp = createBump(temp_ptr, TEMP_MEMORY_SIZE),
		.general = createBump(general_ptr, GENERAL_MEMORY_SIZE),
	};
	return mem;
}
