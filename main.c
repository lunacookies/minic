#include "minic.h"

int main()
{
	memory m = initMemory();
	initializeDiagnosticSink();

	projectSpec current_project = discoverProject(&m);
	setCurrentProject(current_project);

	for (u16 i = 0; i < current_project.num_files; i++) {
		setCurrentFile(i);

		printf("name: %s\n", current_project.file_names[i]);
		printf("content: %s", current_project.file_contents[i]);

		u8 *content = current_project.file_contents[i];
		tokenBuffer tokens = lex(content, &m);
		astRoot ast = parse(tokens, content, &m);
		debugAst(ast);
	}
}
