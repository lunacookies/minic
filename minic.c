#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dir.h>

char *
getext(char *path)
{
	for (; path[0] != '\0'; path++) {
		if (path[0] == '.') {
			if (path[1] == '\0')
				break;
			return path + 1;
		}
	}
	return NULL;
}

int
main(void)
{
	DIR *dirstream = opendir(".");

	while (true) {
		struct dirent *entry = readdir(dirstream);
		if (entry == NULL)
			break;
		if (entry->d_type != DT_REG)
			continue;

		char *ext = getext(entry->d_name);
		if (ext == NULL || strcmp(ext, "minic") != 0)
			continue;

		printf("%s\n", entry->d_name);
	}

	closedir(dirstream);
}
