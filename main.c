#include "minic.h"

int main()
{
	memory m = initMemory();
	initializeDiagnosticSink();

	projectSpec current_project = discoverProject(&m);
	assert(m.temp.bytes_used == 0);

	setCurrentProject(current_project);
	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);

		u8 *content = current_project.file_contents[i];
		tokenBuffer tokens = lex(content, &m);
		astRoot ast = parse(tokens, content, &m);
		debugAst(ast);

		assert(m.temp.bytes_used == 0);
	}
}
