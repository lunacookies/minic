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
		u8 *name = allocateInBump(&m->general,
					  entry->d_namlen +
						  1); // for null terminator
		memcpy(name, entry->d_name, entry->d_namlen + 1);

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
	u8 **permanent_file_names = allocateInBump(&m->general, bytes_used);
	u8 **permanent_file_contents = allocateInBump(&m->general, bytes_used);
	memcpy(permanent_file_names, file_names, bytes_used);
	memcpy(permanent_file_contents, file_contents, bytes_used);
	clearBumpToMark(&m->temp, mark);

	projectSpec p = {
		.num_files = num_files,
		.file_names = permanent_file_names,
		.file_contents = permanent_file_contents,
	};
	return p;
}

_Thread_local projectSpec current_project;
_Thread_local u16 current_file;

void setCurrentProject(projectSpec p)
{
	current_project = p;
}

projectSpec currentProject(void)
{
	return current_project;
}

void setCurrentFile(u16 f)
{
	current_file = f;
}

u16 currentFile(void)
{
	return current_file;
}
