#include "minic.h"

enum { MAX_FILES = 1 << 16 };

projectSpec projectDiscover(memory *m)
{
	u16 num_files = 0;

	// We have two “auxiliary arrays” which store information about each
	// file -- a pointer to the name of the file, and a pointer to the
	// contents of the file. We allocate these aux arrays in temporary
	// memory for now since allocations are occurring in general memory
	// while the aux arrays are being populated.
	bumpMark mark = bumpCreateMark(&m->temp);
	char **file_names = bumpAllocateArray(char *, &m->temp, MAX_FILES);
	char **file_contents = bumpAllocateArray(char *, &m->temp, MAX_FILES);

	DIR *d = opendir(".");

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

		int fd = open(entry->d_name, O_RDONLY);

		struct stat stat;
		fstat(fd, &stat);
		usize size = stat.st_size;

		// Copy file name into general memory.
		char *name = bumpCopyArray(char, &m->general, entry->d_name,
					   entry->d_namlen +
						   1); // for null terminator

		// Read file content into general memory.
		char *content =
			bumpAllocateArray(char, &m->general,
					  size + 1); // for null terminator
		usize bytes_read = read(fd, content, size);
		assert(bytes_read == size);
		content[size] = 0;
		close(fd);

		// Store pointers into general memory in aux arrays.
		file_names[num_files] = name;
		file_contents[num_files] = content;
		num_files++;
	}

	// Now that general memory isn’t being touched anymore, we can copy the
	// aux arrays there.
	file_names = bumpCopyArray(char *, &m->general, file_names, num_files);
	file_contents =
		bumpCopyArray(char *, &m->general, file_contents, num_files);
	bumpClearToMark(&m->temp, mark);

	return (projectSpec){
		.num_files = num_files,
		.file_names = file_names,
		.file_contents = file_contents,
	};
}

_Thread_local projectSpec current_project;
_Thread_local bool current_project_initialized;
_Thread_local u16 current_file;
_Thread_local bool current_file_initialized;

void setCurrentProject(projectSpec p)
{
	current_project = p;
	current_project_initialized = true;
}

projectSpec currentProject(void)
{
	assert(current_project_initialized);
	return current_project;
}

void setCurrentFile(u16 f)
{
	current_file = f;
	current_file_initialized = true;
}

u16 currentFile(void)
{
	assert(current_file_initialized);
	return current_file;
}
