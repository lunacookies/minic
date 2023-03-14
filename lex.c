#include "minic.h"

static const char *keywords[] = { "func", "return", "var",  "set",
				  "if",	  "else",   "while" };
static const tokenKind keywordKinds[] = { TOK_FUNC, TOK_RETURN, TOK_VAR,
					  TOK_SET,  TOK_IF,	TOK_ELSE,
					  TOK_WHILE };

static const char twoCharTokens[][2] = {
	{ '=', '=' }, { '!', '=' }, { '<', '=' }, { '>', '=' }
};
static const tokenKind twoCharTokenKinds[] = { TOK_EQUAL_EQUAL, TOK_BANG_EQUAL,
					       TOK_LANGLE_EQUAL,
					       TOK_RANGLE_EQUAL };

static const char oneCharTokens[] = { '=', '+', '-', '*', '/', '{', '}',
				      '(', ')', '<', '>', ':', ';' };
static const tokenKind oneCharTokenKinds[] = {
	TOK_EQUAL,  TOK_PLUS,	TOK_DASH,   TOK_STAR,	TOK_SLASH,
	TOK_LBRACE, TOK_RBRACE, TOK_LPAREN, TOK_RPAREN, TOK_LANGLE,
	TOK_RANGLE, TOK_COLON,	TOK_SEMI
};

static void pushToken(tokenKind kind, u32 start, u32 end, tokenBuffer *buf,
		      memory *m)
{
	tokenKind *k = bumpAllocate(&m->general, sizeof(tokenKind));
	*k = kind;

	span *span = bumpAllocate(&m->temp, sizeof(span));
	span->start = start;
	span->end = end;

	buf->count++;
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
	usize keywords_count = sizeof(keywords) / sizeof(keywords[0]);

	for (usize i = 0; i < buf->count; i++) {
		tokenKind *kind = &buf->kinds[i];

		if (*kind != TOK_IDENTIFIER)
			continue;

		span span = buf->spans[i];
		usize length = span.end - span.start;

		for (usize j = 0; j < keywords_count; j++) {
			if (strlen(keywords[j]) != length)
				continue;
			if (strncmp((char *)input + span.start, keywords[j],
				    length) != 0)
				continue;
			*kind = keywordKinds[j];
			break;
		}
	}
}

tokenBuffer lex(u8 *input, diagnosticsStorage *diagnostics, memory *m)
{
	bumpMark mark = bumpCreateMark(&m->temp);

	tokenBuffer buf = {
		.kinds = (tokenKind *)(m->general.top + m->general.bytes_used),
		.spans = (span *)(m->temp.top + m->temp.bytes_used),
		.identifier_ids = NULL,
		.count = 0,
	};

	usize i = 0;

	while (input[i] != 0) {
		if (isWhitespace(input[i])) {
			i++;
			goto next;
		}

		if (isDigit(input[i])) {
			u32 start = i;
			while (isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(TOK_NUMBER, start, end, &buf, m);
			goto next;
		}

		if (isIdentifierFirst(input[i])) {
			u32 start = i;
			while (isIdentifierFirst(input[i]) || isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(TOK_IDENTIFIER, start, end, &buf, m);
			goto next;
		}

		usize two_char_tokens_count =
			sizeof(twoCharTokens) / sizeof(twoCharTokens[0]);
		for (usize j = 0; j < two_char_tokens_count; j++) {
			char first = twoCharTokens[j][0];
			char second = twoCharTokens[j][1];
			u32 start = i;
			if (input[i] != first)
				continue;
			if (input[i + 1] != second)
				continue;
			i += 2;
			u32 end = i;
			pushToken(twoCharTokenKinds[j], start, end, &buf, m);
			goto next;
		}

		usize one_char_tokens_count =
			sizeof(oneCharTokens) / sizeof(oneCharTokens[0]);
		for (usize j = 0; j < one_char_tokens_count; j++) {
			if (input[i] != oneCharTokens[j])
				continue;
			u32 start = i;
			i++;
			u32 end = i;
			pushToken(oneCharTokenKinds[j], start, end, &buf, m);
			goto next;
		}

		span span = {
			.start = i,
			.end = i + 1,
		};
		diagnosticsStorageRecord(diagnostics, DIAG_ERROR, span,
					 "invalid token “%c”", input[i]);
		i++;
		pushToken(TOK_ERROR, span.start, span.end, &buf, m);

	next:;
	}

	convertKeywords(input, &buf);

	// copy spans from temp memory into general memory
	buf.spans = bumpCopy(&m->general, buf.spans, sizeof(span) * buf.count);
	bumpClearToMark(&m->temp, mark);

	usize identifier_ids_size = buf.count * sizeof(u32);
	buf.identifier_ids = bumpAllocate(&m->general, identifier_ids_size);
	memset(buf.identifier_ids, -1, identifier_ids_size);

	return buf;
}

u8 *tokenKindShow(tokenKind kind)
{
	switch (kind) {
	case TOK_EOF:
		return (u8 *)"EOF";
	case TOK_ERROR:
		return (u8 *)"unrecognized token";
	case TOK_NUMBER:
		return (u8 *)"number literal";
	case TOK_IDENTIFIER:
		return (u8 *)"identifier";
	case TOK_FUNC:
		return (u8 *)"“func”";
	case TOK_RETURN:
		return (u8 *)"“return”";
	case TOK_VAR:
		return (u8 *)"“var”";
	case TOK_SET:
		return (u8 *)"“set”";
	case TOK_IF:
		return (u8 *)"“if”";
	case TOK_ELSE:
		return (u8 *)"“else”";
	case TOK_WHILE:
		return (u8 *)"“while”";
	case TOK_EQUAL:
		return (u8 *)"“=”";
	case TOK_EQUAL_EQUAL:
		return (u8 *)"“==”";
	case TOK_BANG_EQUAL:
		return (u8 *)"“!=”";
	case TOK_PLUS:
		return (u8 *)"“+”";
	case TOK_DASH:
		return (u8 *)"“-”";
	case TOK_STAR:
		return (u8 *)"“*”";
	case TOK_SLASH:
		return (u8 *)"“/”";
	case TOK_LBRACE:
		return (u8 *)"“{”";
	case TOK_RBRACE:
		return (u8 *)"“}”";
	case TOK_LPAREN:
		return (u8 *)"“(”";
	case TOK_RPAREN:
		return (u8 *)"“)”";
	case TOK_LANGLE:
		return (u8 *)"“<”";
	case TOK_LANGLE_EQUAL:
		return (u8 *)"“<=”";
	case TOK_RANGLE:
		return (u8 *)"“>”";
	case TOK_RANGLE_EQUAL:
		return (u8 *)"“>=”";
	case TOK_COLON:
		return (u8 *)"“:”";
	case TOK_SEMI:
		return (u8 *)"“;”";
	}
}

u8 *tokenKindDebug(tokenKind kind)
{
	switch (kind) {
	case TOK_EOF:
		return (u8 *)"EOF";
	case TOK_ERROR:
		return (u8 *)"ERROR";
	case TOK_NUMBER:
		return (u8 *)"NUMBER";
	case TOK_IDENTIFIER:
		return (u8 *)"IDENTIFIER";
	case TOK_FUNC:
		return (u8 *)"FUNC";
	case TOK_RETURN:
		return (u8 *)"RETURN";
	case TOK_VAR:
		return (u8 *)"VAR";
	case TOK_SET:
		return (u8 *)"SET";
	case TOK_IF:
		return (u8 *)"IF";
	case TOK_ELSE:
		return (u8 *)"ELSE";
	case TOK_WHILE:
		return (u8 *)"WHILE";
	case TOK_EQUAL:
		return (u8 *)"EQUAL";
	case TOK_EQUAL_EQUAL:
		return (u8 *)"EQUAL_EQUAL";
	case TOK_BANG_EQUAL:
		return (u8 *)"BANG_EQUAL";
	case TOK_PLUS:
		return (u8 *)"PLUS";
	case TOK_DASH:
		return (u8 *)"DASH";
	case TOK_STAR:
		return (u8 *)"STAR";
	case TOK_SLASH:
		return (u8 *)"SLASH";
	case TOK_LBRACE:
		return (u8 *)"LBRACE";
	case TOK_RBRACE:
		return (u8 *)"RBRACE";
	case TOK_LPAREN:
		return (u8 *)"LPAREN";
	case TOK_RPAREN:
		return (u8 *)"RPAREN";
	case TOK_LANGLE:
		return (u8 *)"LANGLE";
	case TOK_LANGLE_EQUAL:
		return (u8 *)"LANGLE_EQUAL";
	case TOK_RANGLE:
		return (u8 *)"RANGLE";
	case TOK_RANGLE_EQUAL:
		return (u8 *)"RANGLE_EQUAL";
	case TOK_COLON:
		return (u8 *)"COLON";
	case TOK_SEMI:
		return (u8 *)"SEMI";
	}
}

void tokenBufferDebug(tokenBuffer buf, stringBuilder *sb)
{
	stringBuilderPrintf(sb, "{");

	for (usize i = 0; i < buf.count; i++) {
		span span = buf.spans[i];
		tokenKind kind = buf.kinds[i];
		stringBuilderPrintf(sb, "\n\t%s %u..%u", tokenKindDebug(kind),
				    span.start, span.end);

		u32 id = buf.identifier_ids[i].raw;
		if (id == (u32)-1)
			continue;
		stringBuilderPrintf(sb, " (id: %u)", id);
	}

	stringBuilderPrintf(sb, "\n}\n");
}

void tokenBufferDebugPrint(tokenBuffer buf, bump *b)
{
	bumpMark mark = bumpCreateMark(b);
	stringBuilder sb = stringBuilderCreate(b);
	tokenBufferDebug(buf, &sb);
	printf("%s", stringBuilderFinish(sb));
	bumpClearToMark(b, mark);
}

u8 *lexTests(u8 *input, memory *m)
{
	diagnosticsStorage diagnostics = diagnosticsStorageCreate(&m->general);
	tokenBuffer buf = lex(input, &diagnostics, m);
	stringBuilder sb = stringBuilderCreate(&m->temp);
	tokenBufferDebug(buf, &sb);
	diagnosticsStorageDebug(diagnostics, &sb);
	return stringBuilderFinish(sb);
}
