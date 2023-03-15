#include "minic.h"

int main(int argc, char **argv)
{
	memory m = memoryCreate();
	bump assembly_bump = allocateFromOs(16 * 1024 * 1024);
	stringBuilder assembly = stringBuilderCreate(&assembly_bump);
	diagnosticsStorage diagnostics = diagnosticsStorageCreate(&m.general);

	if (argc == 2 && strcmp(argv[1], "--test") == 0) {
		runTests((u8 *)"tests_lex", lexTests, &m.temp);
		assert(m.temp.bytes_used == 0);
		runTests((u8 *)"tests_parse", parseTests, &m.temp);
		assert(m.temp.bytes_used == 0);
		runTests((u8 *)"tests_lower", lowerTests, &m.temp);
		assert(m.temp.bytes_used == 0);
		return 0;
	}

	bool debug = argc == 2 && strcmp(argv[1], "-d") == 0;

	projectSpec current_project = projectDiscover(&m);
	assert(m.temp.bytes_used == 0);

	setCurrentProject(current_project);

	tokenBuffer *token_buffers = bumpAllocateArray(
		tokenBuffer, &m.general, current_project.num_files);

	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);
		u8 *content = current_project.file_contents[i];
		tokenBuffer tokens = lex(content, &diagnostics, &m);
		token_buffers[i] = tokens;
		assert(m.temp.bytes_used == 0);
	}

	interner interner = intern(token_buffers, current_project.file_contents,
				   current_project.num_files, &m);

	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);

		astRoot ast = parse(token_buffers[i],
				    current_project.file_contents[i],
				    &diagnostics, &m);
		if (debug)
			astDebugPrint(ast, interner, &m.temp);

		hirRoot hir = lower(ast, &diagnostics, &m);
		if (debug)
			hirDebugPrint(hir, interner, &m.temp);

		codegen(hir, interner, &assembly, &diagnostics, &m);

		assert(m.temp.bytes_used == 0);
	}

	bumpMark mark = bumpCreateMark(&m.temp);
	stringBuilder sb = stringBuilderCreate(&m.temp);
	diagnosticsStorageShow(diagnostics, &sb);
	printf("%s", stringBuilderFinish(sb));
	bumpClearToMark(&m.temp, mark);

	stringBuilderFinish(assembly);

	if (debug) {
		debugLog("compiled %u files using", current_project.num_files);
		debugLog("    %zu bytes of general memory (%zu bytes padding)",
			 m.general.bytes_used, m.general.padding_bytes_used);
		debugLog("    %zu bytes of assembly", assembly_bump.bytes_used);
	}

	for (u16 i = 0; i < diagnostics.count; i++)
		if (diagnostics.severities[i] == DIAG_ERROR)
			return 1;

	int fd = open("out.s", O_WRONLY | O_CREAT | O_TRUNC, 0666);

	// We subtract 1 to cut off the null terminator
	// added by stringBuilderFinish.
	write(fd, assembly_bump.top, assembly_bump.bytes_used - 1);

	system("as -o out.o out.s");
	system("ld -o out -syslibroot "
	       "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -lSystem "
	       "out.o");
}
