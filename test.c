#include "minic.h"

static char *readFile(char *name, bump *b)
{
	int fd = open(name, O_RDONLY);
	struct stat s;
	fstat(fd, &s);
	usize size = s.st_size;

	char *content = bumpAllocateArray(char, b, size + 1);
	usize bytes_read = read(fd, content, size);
	assert(bytes_read == size);
	content[size] = 0;

	close(fd);
	return content;
}

static bool exists(char *name)
{
	struct stat s;
	return stat(name, &s) == 0;
}

void runTests(const char *dir_name, transformer t, bump *b)
{
	bumpMark mark = bumpCreateMark(b);

	bump transformer_general = bumpCreateSubBump(b, 256 * 1024);
	bump transformer_temp = bumpCreateSubBump(b, 4 * 1024 * 1024);
	bumpMark transformer_general_top = bumpCreateMark(&transformer_general);
	bumpMark transformer_temp_top = bumpCreateMark(&transformer_temp);
	memory transformer_memory = {
		.general = transformer_general,
		.temp = transformer_temp,
	};

	DIR *d = opendir(dir_name);
	for (;;) {
		struct dirent *entry = readdir(d);
		if (entry == NULL)
			break;
		if (entry->d_type != DT_REG)
			continue;

		if (entry->d_namlen < 4)
			continue;
		bool correct_extension =
			entry->d_name[entry->d_namlen - 3] == '.' &&
			entry->d_name[entry->d_namlen - 2] == 'm' &&
			entry->d_name[entry->d_namlen - 1] == 'c';
		if (!correct_extension)
			continue;

		bumpMark local_mark = bumpCreateMark(b);

		char *path = bumpPrintf(b, "%s/%s", dir_name, entry->d_name);
		char *expected_path = bumpPrintf(b, "%s.expected", path);
		char *actual_path = bumpPrintf(b, "%s.actual", path);

		char *source_code = readFile(path, b);

		char *dir_name_unconstified = bumpPrintf(b, dir_name);
		setCurrentProject((projectSpec){
			.num_files = 1,
			.file_names = &dir_name_unconstified,
			.file_contents = &source_code,
		});
		setCurrentFile(0);

		bumpClearToMark(&transformer_memory.general,
				transformer_general_top);
		bumpClearToMark(&transformer_memory.temp, transformer_temp_top);
		char *actual = t(source_code, &transformer_memory);

		if (!exists(expected_path)) {
			printf("\033[35mwarning:\033[0;1;97m “expected” file "
			       "%s "
			       "missing; creating\033[0m\n",
			       expected_path);
			int fd = open(expected_path,
				      O_WRONLY | O_CREAT | O_TRUNC, 0666);
			write(fd, actual, strlen(actual));
			close(fd);
		}

		char *expected = readFile(expected_path, b);

		if (strcmp(expected, actual) == 0) {
			printf("\033[32mtest passed:\033[0;1;97m %s\033[0m\n",
			       path);
			if (exists(actual_path)) {
				printf("\033[35mwarning:\033[0;1;97m stale "
				       "“actual” file %s; deleting\033[0m\n",
				       actual_path);
				remove(actual_path);
			}
		} else {
			int fd = open(actual_path, O_WRONLY | O_CREAT | O_TRUNC,
				      0666);
			write(fd, actual, strlen(actual));
			close(fd);

			printf("\033[31mtest failed:\033[0;1;97m %s\033[0m\n",
			       path);

			char *command =
				bumpPrintf(b, "diff -u --color=auto %s %s",
					   expected_path, actual_path);
			system(command);
		}

		bumpClearToMark(b, local_mark);
	}

	bumpClearToMark(b, mark);
}
