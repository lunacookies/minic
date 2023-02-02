#include "minic.h"

static const char *keywords[] = { "func", "return", "var" };
static const tokenKind keywordKinds[] = { TOK_FUNC, TOK_RETURN, TOK_VAR };

static void pushToken(tokenKind kind, u32 start, u32 end, tokenBuffer *buf,
		      memory *m)
{
	tokenKind *k = allocateInBump(&m->general, sizeof(tokenKind));
	*k = kind;

	span *span = allocateInBump(&m->temp, sizeof(span));
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
	usize num_keywords = sizeof(keywords) / sizeof(keywords[0]);

	for (usize i = 0; i < buf->count; i++) {
		tokenKind *kind = &buf->kinds[i];

		if (*kind != TOK_IDENTIFIER)
			continue;

		span span = buf->spans[i];
		usize length = span.end - span.start;

		for (usize j = 0; j < num_keywords; j++)
			if (strncmp((char *)input + span.start, keywords[j],
				    length) == 0)
				*kind = keywordKinds[j];
	}
}

typedef struct slot {
	u32 length;
	u8 *ptr;
	u64 hash;
} slot;

static void internIdentifiers(tokenBuffer *buf, u8 *input, memory *m)
{
	// count number of identifiers
	usize identifier_count = 0;
	for (usize i = 0; i < buf->count; i++)
		if (buf->kinds[i] == TOK_IDENTIFIER)
			identifier_count++;

	// allocate space for load factor of 0.75
	usize slot_count = identifier_count * 4 / 3;
	usize map_size = slot_count * sizeof(slot);
	clearBump(&m->temp);
	slot *map = allocateInBump(&m->temp, map_size);
	memset(map, 0, map_size); // each ptr in map starts out as null

	identifierId *identifier_ids =
		allocateInBump(&m->general, buf->count * sizeof(u32));
	u8 **identifier_contents =
		allocateInBump(&m->general, identifier_count * sizeof(u8 *));
	identifierId ident_id = { .raw = 0 };

	for (usize token_index = 0; token_index < buf->count; token_index++) {
		// tokens that are not identifiers
		// have a sentinel identifier ID of -1
		identifier_ids[token_index].raw = -1;

		if (buf->kinds[token_index] != TOK_IDENTIFIER)
			continue;

		span span = buf->spans[token_index];
		u32 length = span.end - span.start;
		u8 *ptr = input + span.start;
		u64 hash = fxhash(ptr, length);

		usize slot_index = hash % slot_count;
		slot *slot = &map[slot_index];
		while (slot->ptr != NULL) {
			if (slot->hash != hash)
				goto no_match;

			if (slot->length != length)
				goto no_match;

			if (memcmp(ptr, slot->ptr, length) != 0)
				goto no_match;

			// at this point we have compared
			// every possible source of equality we could,
			// and everything has come up matching
			//
			// we have found a slot which contains
			identifier_ids[token_index] = ident_id;
			goto next_token;

		no_match:
			slot_index++;
			slot = &map[slot_index];
		}

		// at this point we’ve exited the loop
		// because we found an empty slot
		// and along the way didn’t stumble upon a slot
		// which matches the current token

		slot->length = length;
		slot->ptr = ptr;
		slot->hash = hash;

		identifier_ids[token_index] = ident_id;

		u8 *contents = allocateInBump(&m->general, length + 1);
		memcpy(contents, ptr, length);
		contents[length] = '\0';
		identifier_contents[ident_id.raw] = contents;

		ident_id.raw++;

	next_token : {
	}
	}

	// we have no use for the map any longer
	// now that identifier IDs have been allocated
	// and identifier contents have been copied
	clearBump(&m->temp);

	buf->identifier_ids = identifier_ids;
	buf->identifier_contents = identifier_contents;
}

tokenBuffer lex(u8 *input, memory *m)
{
	clearBump(&m->temp);

	tokenBuffer buf = {
		.kinds = (tokenKind *)(m->general.top + m->general.bytes_used),
		.spans = (span *)(m->temp.top + m->temp.bytes_used),
		.identifier_ids = NULL,
		.identifier_contents = NULL,
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
			pushToken(TOK_NUMBER, start, end, &buf, m);
			continue;
		}

		if (isIdentifierFirst(input[i])) {
			u32 start = i;
			while (isIdentifierFirst(input[i]) || isDigit(input[i]))
				i++;
			u32 end = i;
			pushToken(TOK_IDENTIFIER, start, end, &buf, m);
			continue;
		}

		if (input[i] == '=') {
			u32 start = i;
			i++;
			u32 end = i;
			pushToken(TOK_EQUAL, start, end, &buf, m);
			continue;
		}
		if (input[i] == '{') {
			u32 start = i;
			i++;
			u32 end = i;
			pushToken(TOK_LBRACE, start, end, &buf, m);
			continue;
		}
		if (input[i] == '}') {
			u32 start = i;
			i++;
			u32 end = i;
			pushToken(TOK_RBRACE, start, end, &buf, m);
			continue;
		}

		span span = {
			.start = i,
			.end = i + 1,
		};
		sendDiagnosticToSink(DIAG_ERROR, span, "invalid token “%c”",
				     input[i]);
		i++;
		pushToken(TOK_ERROR, span.start, span.end, &buf, m);
	}

	convertKeywords(input, &buf);

	// copy spans from temp memory into general memory
	usize spans_size = sizeof(span) * buf.count;
	span *spans = allocateInBump(&m->general, spans_size);
	memcpy(spans, buf.spans, spans_size);
	buf.spans = spans;
	clearBump(&m->temp);

	internIdentifiers(&buf, input, m);

	return buf;
}

u8 *showTokenKind(tokenKind kind)
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
	case TOK_EQUAL:
		return (u8 *)"“=”";
	case TOK_LBRACE:
		return (u8 *)"“{”";
	case TOK_RBRACE:
		return (u8 *)"“}”";
	}
}

u8 *debugTokenKind(tokenKind kind)
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
	case TOK_EQUAL:
		return (u8 *)"EQUAL";
	case TOK_LBRACE:
		return (u8 *)"LBRACE";
	case TOK_RBRACE:
		return (u8 *)"RBRACE";
	}
}

void debugTokenBuffer(tokenBuffer buf)
{
	printf("\033[1mtokenBuffer\033[0m\n");
	printf("              count: \033[36m%zu\033[0m\n", buf.count);
	printf("              kinds: \033[36m%p\033[0m\n", (void *)buf.kinds);
	printf("              spans: \033[36m%p\033[0m\n", (void *)buf.spans);
	printf("     identifier_ids: \033[36m%p\033[0m\n",
	       (void *)buf.identifier_ids);
	printf("identifier_contents: \033[36m%p\033[0m\n",
	       (void *)buf.identifier_contents);

	printf("{");
	for (usize i = 0; i < buf.count; i++) {
		span s = buf.spans[i];
		printf("\n\t\033[35m%s\033[0m ", debugTokenKind(buf.kinds[i]));
		printf("\033[36m%u\033[0m..", s.start);
		printf("\033[36m%u\033[0m", s.end);

		u32 id = buf.identifier_ids[i].raw;
		if (id == (u32)-1)
			continue;

		printf(" (id: \033[36m%u\033[0m, ", id);
		printf("text: \033[32m\"%s\"\033[0m)",
		       buf.identifier_contents[id]);
	}
	printf("\n}\n");
}
