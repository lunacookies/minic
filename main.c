#include "minic.h"

int main()
{
	memory m = initMemory();
	projectSpec project_spec = discoverProject(&m);
	for (usize i = 0; i < project_spec.num_files; i++) {
		printf("name: %s\n", project_spec.file_names[i]);
		printf("content: %s", project_spec.file_contents[i]);
	}
}
