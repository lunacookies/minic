#include "minic.h"

#define MiB (1024 * 1024)
#define TEMP_MEMORY_SIZE (16 * MiB)
#define ASSEMBLY_MEMORY_SIZE (16 * MiB)
#define GENERAL_MEMORY_SIZE (96 * MiB)

static u8 *allocFromOs(usize size, u8 *desired_addr)
{
	u8 *ptr = mmap(desired_addr, size, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	assert(ptr == desired_addr);
	return ptr;
}

memory initMemory(void)
{
	u8 *temp_ptr = allocFromOs(TEMP_MEMORY_SIZE, (u8 *)0x0000200000000000);
	u8 *assembly_ptr =
		allocFromOs(ASSEMBLY_MEMORY_SIZE, (u8 *)0x0000300000000000);
	u8 *general_ptr =
		allocFromOs(GENERAL_MEMORY_SIZE, (u8 *)0x0000400000000000);

	return (memory){
		.temp = createBump(temp_ptr, TEMP_MEMORY_SIZE),
		.assembly = createBump(assembly_ptr, ASSEMBLY_MEMORY_SIZE),
		.general = createBump(general_ptr, GENERAL_MEMORY_SIZE),
	};
}
