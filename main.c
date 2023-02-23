#include "minic.h"

int main(int argc, char **argv)
{
	bool debug = argc == 2 && strcmp(argv[1], "-d") == 0;

	memory m = initMemory();
	bump assembly = allocateFromOs(16 * 1024 * 1024);
	initializeDiagnostics(&m);

	projectSpec current_project = discoverProject(&m);
	assert(m.temp.bytes_used == 0);

	setCurrentProject(current_project);

	tokenBuffer *token_buffers = allocateInBump(
		&m.general, sizeof(tokenBuffer) * current_project.num_files);

	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);
		u8 *content = current_project.file_contents[i];
		tokenBuffer tokens = lex(content, &m);
		token_buffers[i] = tokens;
		assert(m.temp.bytes_used == 0);
	}

	usize identifier_count = 0;
	for (u16 i = 0; i < current_project.num_files; i++) {
		tokenBuffer buf = token_buffers[i];
		for (usize j = 0; j < buf.count; j++)
			if (buf.kinds[j] == TOK_IDENTIFIER)
				identifier_count++;
	}

	interner interner =
		intern(token_buffers, current_project.file_contents,
		       current_project.num_files, identifier_count, &m);

	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);

		astRoot ast = parse(token_buffers[i],
				    current_project.file_contents[i], &m);
		if (debug)
			debugPrintAst(ast, interner, &m.temp);

		hirRoot hir = lower(ast, &m);
		if (debug)
			debugPrintHir(hir, interner, &m.temp);

		codegen(hir, interner, &assembly, &m);

		assert(m.temp.bytes_used == 0);
	}

	bumpMark mark = markBump(&m.temp);
	u8 *s = showDiagnostics(&m.temp, true);
	printf("%s", s);
	clearBumpToMark(&m.temp, mark);

	if (debug) {
		debugLog("compiled %u files using", current_project.num_files);
		debugLog("    %zu bytes of general memory",
			 m.general.bytes_used);
		debugLog("    %zu bytes of assembly", assembly.bytes_used);
	}

	if (anyErrors())
		return 1;

	int fd = open("out.s", O_WRONLY | O_CREAT | O_TRUNC, 0666);
	write(fd, assembly.top, assembly.bytes_used);
	system("as -o out.o out.s");
	system("ld -o out -syslibroot "
	       "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -lSystem "
	       "out.o");
}
