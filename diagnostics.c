#include "minic.h"

pthread_mutex_t stderrMutex;
bool isInitialized = false;

void initializeDiagnosticSink(void)
{
	assert(!isInitialized);
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutex_init(&stderrMutex, &attr);
	isInitialized = true;
}

// 0-indexed
typedef struct lineColumn {
	u32 line;
	u32 column;
} lineColumn;

typedef struct diagnosticLocation {
	u8 *name;
	lineColumn lc;
} diagnosticLocation;

static lineColumn offsetToLineColumn(u32 offset, u8 *content)
{
	lineColumn lc = {
		.line = 0,
		.column = 0,
	};

	for (u32 i = 0; i < offset; i++) {
		assert(content[i] != 0);
		lc.column++;

		if (content[i] == '\n') {
			lc.line++;
			lc.column = 0;
		}
	}

	return lc;
}

static diagnosticLocation getDiagnosticLocation(span span)
{
	projectSpec current_project = currentProject();
	u16 current_file = currentFile();

	u8 *content = current_project.file_contents[current_file];

	diagnosticLocation loc = {
		.name = current_project.file_names[current_file],
		.lc = offsetToLineColumn(span.start, content),
	};
	return loc;
}

void sendDiagnosticToSink(severity severity, span span, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	sendDiagnosticToSinkV(severity, span, fmt, ap);
	va_end(ap);
}

void sendDiagnosticToSinkV(severity severity, span span, char *fmt, va_list ap)
{
	assert(isInitialized);
	pthread_mutex_lock(&stderrMutex);

	diagnosticLocation loc = getDiagnosticLocation(span);
	fprintf(stderr, "%s:%u:%u: ", loc.name, loc.lc.line + 1,
		loc.lc.column + 1);

	switch (severity) {
	case DIAG_WARNING:
		fprintf(stderr, "\033[33mwarning"); // yellow
		break;
	case DIAG_ERROR:
		fprintf(stderr, "\033[31merror"); // red
		break;
	}
	fprintf(stderr, ":\033[0;1m ");

	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\033[0m\n");

	pthread_mutex_unlock(&stderrMutex);
}
