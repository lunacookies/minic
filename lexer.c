#include "minic.h"

static const char *keywords[] = { "func", "return" };
static const tokenKind keywordKinds[] = { TOK_FUNC, TOK_RETURN };

static void pushToken(tokenKind kind, u32 start, u32 end, memory *m)
{
	token t = {
		.kind = kind,
		.span = {
			.start = start,
			.end = end,
		},
	};
	token *p = allocateInBump(&m->general, sizeof(token));
	*p = t;
}

static bool isWhitespace(u8 c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

static bool isDigit(u8 c)
{
	return c >= '0' && c <= '9';
}

static bool isIdentifierFirst(u8 c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

static void convertKeywords(u8 *input, tokenBuffer *buf)
{
	usize num_keywords = sizeof(keywords) / sizeof(keywords[0]);

	for (usize i = 0; i < buf->count; i++) {
		token *t = &buf->tokens[i];
		if (t->kind != TOK_IDENTIFIER)
			continue;

		usize length = t->span.end - t->span.start;

		for (usize j = 0; j < num_keywords; j++)
			if (strncmp((char *)input + t->span.start, keywords[j],
				    length) == 0)
				t->kind = keywordKinds[j];
	}
}

tokenBuffer lex(u8 *input, memory *m)
{
	tokenBuffer buf = {
		.tokens = (token *)(m->general.top + m->general.bytes_used),
		.count = 0,
	};

	usize i = 0;

	while (input[i] != '\0') {
		if (isWhitespace(input[i])) {
			i++;
			continue;
		}

		if (isDigit(input[i])) {
			u32 start = i;
			while (isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(TOK_NUMBER, start, end, m);
			buf.count++;
			continue;
		}

		if (isIdentifierFirst(input[i])) {
			u32 start = i;
			while (isIdentifierFirst(input[i]) || isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(TOK_IDENTIFIER, start, end, m);
			buf.count++;
			continue;
		}

		span span = {
			.start = i,
			.end = i + 1,
		};
		sendDiagnosticToSink(DIAG_ERROR, span, "invalid token “%c”",
				     input[i]);
		i++;
		pushToken(TOK_ERROR, span.start, span.end, m);
		buf.count++;
	}

	convertKeywords(input, &buf);

	return buf;
}
