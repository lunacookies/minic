#include "minic.h"

int main()
{
	memory m = initMemory();
	initializeDiagnosticSink();

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
		debugAst(ast, interner);
		hirRoot hir = lower(ast, &m);
		debugHir(hir, interner);

		assert(m.temp.bytes_used == 0);
	}

	debugLog("compiled %u files using %zu bytes of memory",
		 current_project.num_files, m.general.bytes_used);
}
