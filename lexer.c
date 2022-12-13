#include "minic.h"

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

		error("invalid token");
	}

	return buf;
}
