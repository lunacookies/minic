#include "minic.h"

enum { MAX_DIAGNOSTIC_COUNT = 1024 };

diagnosticsStorage diagnosticsStorageCreate(bump *b)
{
	return (diagnosticsStorage){
		.files = bumpAllocate(b, sizeof(u16) * MAX_DIAGNOSTIC_COUNT),
		.spans = bumpAllocate(b, sizeof(span) * MAX_DIAGNOSTIC_COUNT),
		.severities = bumpAllocate(b, sizeof(severity) *
						      MAX_DIAGNOSTIC_COUNT),
		.message_starts =
			bumpAllocate(b, sizeof(u32) * MAX_DIAGNOSTIC_COUNT),
		.all_messages =
			bumpCreateSubBump(b, 128 * MAX_DIAGNOSTIC_COUNT),
		.count = 0,
	};
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

void diagnosticsStorageRecord(diagnosticsStorage *diagnostics,
			      severity severity, span span, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	diagnosticsStorageRecordV(diagnostics, severity, span, fmt, ap);
	va_end(ap);
}

void diagnosticsStorageRecordV(diagnosticsStorage *diagnostics,
			       severity severity, span span, char *fmt,
			       va_list ap)
{
	assert(diagnostics->count < MAX_DIAGNOSTIC_COUNT);

	u8 *message = bumpPrintfV(&diagnostics->all_messages, fmt, ap);
	u32 message_start = message - diagnostics->all_messages.top;

	diagnostics->files[diagnostics->count] = currentFile();
	diagnostics->spans[diagnostics->count] = span;
	diagnostics->severities[diagnostics->count] = severity;
	diagnostics->message_starts[diagnostics->count] = message_start;
	diagnostics->count++;
}

void diagnosticsStorageShow(diagnosticsStorage diagnostics, stringBuilder *sb)
{
	for (u16 i = 0; i < diagnostics.count; i++) {
		u16 file = diagnostics.files[i];
		u8 *file_name = currentProject().file_names[file];
		u8 *file_content = currentProject().file_contents[file];

		span span = diagnostics.spans[i];

		lineColumn start_lc =
			offsetToLineColumn(span.start, file_content);
		stringBuilderPrintf(sb, "\033[90m%s:%u:%u:\033[m ", file_name,
				    start_lc.line + 1, start_lc.column + 1);

		switch (diagnostics.severities[i]) {
		case DIAG_WARNING:
			stringBuilderPrintf(sb, "\033[95m"); // magenta
			stringBuilderPrintf(sb, "warning");
			break;
		case DIAG_ERROR:
			stringBuilderPrintf(sb, "\033[31m"); // red
			stringBuilderPrintf(sb, "error");
			break;
		}
		stringBuilderPrintf(sb, ": \033[1;97m");

		u32 message_start = diagnostics.message_starts[i];
		u8 *message = diagnostics.all_messages.top + message_start;
		stringBuilderPrintf(sb, "%s\033[m\n", message);

		usize line_start = 0;
		usize line_end = 0;

		for (usize j = 0; file_content[j] != 0; j++) {
			if (file_content[j] != '\n')
				continue;

			if (j < span.start) {
				line_start = j;
				line_start++; // get rid of leading newline
			}

			if (j >= span.end - 1) {
				line_end = j;
				break;
			}
		}

		usize line_length = line_end - line_start;
		stringBuilderPrintf(sb, "%.*s\n", line_length,
				    &file_content[line_start]);

		stringBuilderPrintf(sb, "\033[92m");
		for (usize j = line_start; j < line_end + 1; j++) {
			if (j == span.start)
				stringBuilderPrintf(sb, "^");
			else if (j >= span.start && j < span.end)
				stringBuilderPrintf(sb, "~");
			else if (file_content[j] == '\t')
				stringBuilderPrintf(sb, "\t");
			else
				stringBuilderPrintf(sb, " ");
		}
		stringBuilderPrintf(sb, "\033[m\n");
	}
}

void diagnosticsStorageDebug(diagnosticsStorage diagnostics, stringBuilder *sb)
{
	for (u16 i = 0; i < diagnostics.count; i++) {
		u16 file = diagnostics.files[i];
		u8 *file_name = currentProject().file_names[file];

		span span = diagnostics.spans[i];
		stringBuilderPrintf(sb, "%s:%u..%u: ", file_name, span.start,
				    span.end);

		switch (diagnostics.severities[i]) {
		case DIAG_WARNING:
			stringBuilderPrintf(sb, "warning");
			break;
		case DIAG_ERROR:
			stringBuilderPrintf(sb, "error");
			break;
		}
		stringBuilderPrintf(sb, ": ");

		u32 message_start = diagnostics.message_starts[i];
		u8 *message = diagnostics.all_messages.top + message_start;
		stringBuilderPrintf(sb, "%s\n", message);
	}
}
