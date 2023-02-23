#include "minic.h"

#define MAX_FILES (1 << 16)
#define PTR_PER_FILE_SIZE (sizeof(void *) * MAX_FILES)

projectSpec discoverProject(memory *m)
{
	u16 num_files = 0;

	// We have two “auxiliary arrays” which store information about each
	// file -- a pointer to the name of the file, and a pointer to the
	// contents of the file. We allocate these aux arrays in temporary
	// memory for now since allocations are occurring in general memory
	// while the aux arrays are being populated.
	bumpMark mark = markBump(&m->temp);
	u8 **file_names = allocateInBump(&m->temp, PTR_PER_FILE_SIZE);
	u8 **file_contents = allocateInBump(&m->temp, PTR_PER_FILE_SIZE);

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
		u8 *name =
			copyInBump(&m->general, entry->d_name,
				   entry->d_namlen + 1); // for null terminator

		// Read file content into general memory.
		u8 *content = allocateInBump(&m->general,
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
	usize bytes_used = num_files * sizeof(u8 *);
	file_names = copyInBump(&m->general, file_names, bytes_used);
	file_contents = copyInBump(&m->general, file_contents, bytes_used);
	clearBumpToMark(&m->temp, mark);

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
