#include "minic.h"

#define MAX_DIAGNOSTIC_COUNT 1024

diagnosticsStorage diagnostics;
pthread_mutex_t mutex;
bool isInitialized = false;
bool anyErrors_ = false;

void initializeDiagnostics(memory *m)
{
	assert(!isInitialized);
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutex_init(&mutex, &attr);

	diagnostics = (diagnosticsStorage){
		.files = allocateInBump(&m->general,
					sizeof(u16) * MAX_DIAGNOSTIC_COUNT),
		.spans = allocateInBump(&m->general,
					sizeof(span) * MAX_DIAGNOSTIC_COUNT),
		.severities = allocateInBump(
			&m->general, sizeof(severity) * MAX_DIAGNOSTIC_COUNT),
		.message_starts = allocateInBump(
			&m->general, sizeof(u32) * MAX_DIAGNOSTIC_COUNT),
		.all_messages =
			createSubBump(&m->general, 128 * MAX_DIAGNOSTIC_COUNT),
		.count = 0,
	};

	isInitialized = true;
}

// 0-indexed
typedef struct lineColumn {
	u32 line;
	u32 column;
} lineColumn;

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

void recordDiagnostic(severity severity, span span, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	recordDiagnosticV(severity, span, fmt, ap);
	va_end(ap);
}

void recordDiagnosticV(severity severity, span span, char *fmt, va_list ap)
{
	assert(isInitialized);
	pthread_mutex_lock(&mutex);
	assert(diagnostics.count < MAX_DIAGNOSTIC_COUNT);

	u8 *message = printfInBumpV(&diagnostics.all_messages, fmt, ap);
	u32 message_start = message - diagnostics.all_messages.top;

	if (severity == DIAG_ERROR)
		anyErrors_ = true;

	diagnostics.files[diagnostics.count] = currentFile();
	diagnostics.spans[diagnostics.count] = span;
	diagnostics.severities[diagnostics.count] = severity;
	diagnostics.message_starts[diagnostics.count] = message_start;
	diagnostics.count++;

	pthread_mutex_unlock(&mutex);
}

bool anyErrors(void)
{
	assert(isInitialized);
	pthread_mutex_lock(&mutex);
	bool b = anyErrors_;
	pthread_mutex_unlock(&mutex);
	return b;
}

void showDiagnostics(bool color, stringBuilder *sb)
{
	assert(isInitialized);
	pthread_mutex_lock(&mutex);

	for (u16 i = 0; i < diagnostics.count; i++) {
		u16 file = diagnostics.files[i];
		u8 *file_name = currentProject().file_names[file];
		u8 *file_content = currentProject().file_contents[file];

		span span = diagnostics.spans[i];
		lineColumn lc = offsetToLineColumn(span.start, file_content);

		printfInStringBuilder(sb, "%s:%u:%u: ", file_name, lc.line + 1,
				      lc.column + 1);

		switch (diagnostics.severities[i]) {
		case DIAG_WARNING:
			if (color)
				printfInStringBuilder(sb, "\033[33m"); // yellow
			printfInStringBuilder(sb, "warning");
			break;
		case DIAG_ERROR:
			if (color)
				printfInStringBuilder(sb, "\033[31m"); // red
			printfInStringBuilder(sb, "error");
			break;
		}
		printfInStringBuilder(sb, ": ");

		if (color)
			printfInStringBuilder(sb, "\033[0;1m");

		u32 message_start = diagnostics.message_starts[i];
		u8 *message = diagnostics.all_messages.top + message_start;
		printfInStringBuilder(sb, "%s", message);

		if (color)
			printfInStringBuilder(sb, "\033[0m");

		printfInStringBuilder(sb, "\n");
	}

	pthread_mutex_unlock(&mutex);
}
