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

		tokenBuffer tokens = lex(current_project.file_contents[i], &m);
		for (usize j = 0; j < tokens.count; j++)
			printf("%d@%u..%u\n", tokens.tokens[j].kind,
			       tokens.tokens[j].span.start,
			       tokens.tokens[j].span.end);
	}
}
