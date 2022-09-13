#include "bump.h"
#include "vec.h"

#include <stdbool.h>
#include <stddef.h>
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
	struct bump b = bump_new();
	struct vec files = vec_new(sizeof(void *));

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

		void *filename = bump_alloc(&b, entry->d_namlen + 1);
		memcpy(filename, &entry->d_name, entry->d_namlen + 1);
		vec_push(&files, &filename);
	}

	closedir(dirstream);

	for (char **file = (char **)files.ptr;
	     file < (char **)files.ptr + files.len; file++) {
		printf("%s\n", *file);
	}
}
