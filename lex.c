#include "minic.h"

static const char *keywords[] = { "func", "return", "var",  "set",
				  "if",	  "else",   "while" };
static const tokenKind keywordKinds[] = { TOK_FUNC, TOK_RETURN, TOK_VAR,
					  TOK_SET,  TOK_IF,	TOK_ELSE,
					  TOK_WHILE };

static const char twoCharTokens[][2] = {
	{ '=', '=' }, { '!', '=' }, { '<', '=' }, { '>', '=' }, { ':', '=' }
};
static const tokenKind twoCharTokenKinds[] = { TOK_EQUAL_EQUAL, TOK_BANG_EQUAL,
					       TOK_LANGLE_EQUAL,
					       TOK_RANGLE_EQUAL,
					       TOK_COLON_EQUAL };

static const char oneCharTokens[] = { '=', '+', '-', '*', '/', '{', '}', '(',
				      ')', '[', ']', '<', '>', ':', ';', '&' };
static const tokenKind oneCharTokenKinds[] = {
	TOK_EQUAL,  TOK_PLUS,	 TOK_DASH,    TOK_STAR,
	TOK_SLASH,  TOK_LBRACE,	 TOK_RBRACE,  TOK_LPAREN,
	TOK_RPAREN, TOK_LSQUARE, TOK_RSQUARE, TOK_LANGLE,
	TOK_RANGLE, TOK_COLON,	 TOK_SEMI,    TOK_AMPERSAND
};

typedef struct lexer {
	arrayBuilder kinds;
	arrayBuilder spans;
	usize count;
} lexer;

static void pushToken(lexer *lexer, tokenKind kind, u32 start, u32 end)
{
	arrayBuilderPush(&lexer->kinds, &kind);
	arrayBuilderPush(&lexer->spans, &(span){ .start = start, .end = end });
	lexer->count++;
}

static bool isWhitespace(char c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

static bool isDigit(char c)
{
	return c >= '0' && c <= '9';
}

static bool isIdentifierFirst(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c == '_');
}

static void convertKeywords(char *input, tokenBuffer *buf)
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
			if (strncmp(input + span.start, keywords[j], length) !=
			    0)
				continue;
			*kind = keywordKinds[j];
			break;
		}
	}
}

tokenBuffer lex(char *input, diagnosticsStorage *diagnostics, memory *m)
{
	bumpMark mark = bumpCreateMark(&m->temp);

	lexer lexer = {
		.kinds = bumpStartArrayBuilder(&m->general, sizeof(tokenKind)),
		.spans = bumpStartArrayBuilder(&m->temp, sizeof(span)),
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
			pushToken(&lexer, TOK_NUMBER, start, end);
			goto next;
		}

		if (isIdentifierFirst(input[i])) {
			u32 start = i;
			while (isIdentifierFirst(input[i]) || isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(&lexer, TOK_IDENTIFIER, start, end);
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
			pushToken(&lexer, twoCharTokenKinds[j], start, end);
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
			pushToken(&lexer, oneCharTokenKinds[j], start, end);
			goto next;
		}

		span span = {
			.start = i,
			.end = i + 1,
		};
		diagnosticsStorageRecord(diagnostics, DIAG_ERROR, span,
					 "invalid token “%c”", input[i]);
		i++;
		pushToken(&lexer, TOK_ERROR, span.start, span.end);

	next:;
	}

	tokenKind *kinds =
		(tokenKind *)bumpFinishArrayBuilder(&m->general, &lexer.kinds);
	span *spans = (span *)bumpFinishArrayBuilder(&m->temp, &lexer.spans);

	// Copy spans from temp memory into general memory.
	spans = bumpCopyArray(span, &m->general, spans, lexer.count);
	bumpClearToMark(&m->temp, mark);

	tokenBuffer buf = {
		.kinds = kinds,
		.spans = spans,
		.identifier_ids = NULL,
		.count = lexer.count,
	};

	convertKeywords(input, &buf);

	buf.identifier_ids =
		bumpAllocateArray(identifierId, &m->general, buf.count);
	memset(buf.identifier_ids, -1, buf.count * sizeof(identifierId));

	return buf;
}

const char *tokenKindShow(tokenKind kind)
{
	switch (kind) {
	case TOK_EOF:
		return "EOF";
	case TOK_ERROR:
		return "unrecognized token";
	case TOK_NUMBER:
		return "number literal";
	case TOK_IDENTIFIER:
		return "identifier";
	case TOK_FUNC:
		return "“func”";
	case TOK_RETURN:
		return "“return”";
	case TOK_VAR:
		return "“var”";
	case TOK_SET:
		return "“set”";
	case TOK_IF:
		return "“if”";
	case TOK_ELSE:
		return "“else”";
	case TOK_WHILE:
		return "“while”";
	case TOK_EQUAL:
		return "“=”";
	case TOK_EQUAL_EQUAL:
		return "“==”";
	case TOK_BANG_EQUAL:
		return "“!=”";
	case TOK_PLUS:
		return "“+”";
	case TOK_DASH:
		return "“-”";
	case TOK_STAR:
		return "“*”";
	case TOK_SLASH:
		return "“/”";
	case TOK_LBRACE:
		return "“{”";
	case TOK_RBRACE:
		return "“}”";
	case TOK_LPAREN:
		return "“(”";
	case TOK_RPAREN:
		return "“)”";
	case TOK_LSQUARE:
		return "“[”";
	case TOK_RSQUARE:
		return "“]”";
	case TOK_LANGLE:
		return "“<”";
	case TOK_LANGLE_EQUAL:
		return "“<=”";
	case TOK_RANGLE:
		return "“>”";
	case TOK_RANGLE_EQUAL:
		return "“>=”";
	case TOK_COLON:
		return "“:”";
	case TOK_COLON_EQUAL:
		return "“:=”";
	case TOK_SEMI:
		return "“;”";
	case TOK_AMPERSAND:
		return "“&”";
	}
}

const char *tokenKindDebug(tokenKind kind)
{
	switch (kind) {
	case TOK_EOF:
		return "EOF";
	case TOK_ERROR:
		return "ERROR";
	case TOK_NUMBER:
		return "NUMBER";
	case TOK_IDENTIFIER:
		return "IDENTIFIER";
	case TOK_FUNC:
		return "FUNC";
	case TOK_RETURN:
		return "RETURN";
	case TOK_VAR:
		return "VAR";
	case TOK_SET:
		return "SET";
	case TOK_IF:
		return "IF";
	case TOK_ELSE:
		return "ELSE";
	case TOK_WHILE:
		return "WHILE";
	case TOK_EQUAL:
		return "EQUAL";
	case TOK_EQUAL_EQUAL:
		return "EQUAL_EQUAL";
	case TOK_BANG_EQUAL:
		return "BANG_EQUAL";
	case TOK_PLUS:
		return "PLUS";
	case TOK_DASH:
		return "DASH";
	case TOK_STAR:
		return "STAR";
	case TOK_SLASH:
		return "SLASH";
	case TOK_LBRACE:
		return "LBRACE";
	case TOK_RBRACE:
		return "RBRACE";
	case TOK_LPAREN:
		return "LPAREN";
	case TOK_RPAREN:
		return "RPAREN";
	case TOK_LSQUARE:
		return "LSQUARE";
	case TOK_RSQUARE:
		return "RSQUARE";
	case TOK_LANGLE:
		return "LANGLE";
	case TOK_LANGLE_EQUAL:
		return "LANGLE_EQUAL";
	case TOK_RANGLE:
		return "RANGLE";
	case TOK_RANGLE_EQUAL:
		return "RANGLE_EQUAL";
	case TOK_COLON:
		return "COLON";
	case TOK_COLON_EQUAL:
		return "COLON_EQUAL";
	case TOK_SEMI:
		return "SEMI";
	case TOK_AMPERSAND:
		return "AMPERSAND";
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

char *lexTests(char *input, memory *m)
{
	diagnosticsStorage diagnostics = diagnosticsStorageCreate(&m->general);
	tokenBuffer buf = lex(input, &diagnostics, m);
	stringBuilder sb = stringBuilderCreate(&m->temp);
	tokenBufferDebug(buf, &sb);
	diagnosticsStorageDebug(diagnostics, &sb);
	return stringBuilderFinish(sb);
}
