#include "minic.h"

typedef struct parser {
	tokenBuffer tokens;
	usize cursor;
	u8 *content;
} parser;

static bool atEof(parser *p)
{
	// cursor should never go more than one past the end
	assert(p->cursor <= p->tokens.count);
	return p->cursor == p->tokens.count;
}

static void addToken(parser *p)
{
	assert(!atEof(p));
	p->cursor++;
}

static tokenKind current(parser *p)
{
	if (atEof(p))
		return TOK_EOF;
	return p->tokens.kinds[p->cursor];
}

static span currentSpan(parser *p)
{
	assert(!atEof(p));
	return p->tokens.spans[p->cursor];
}

static bool at(parser *p, tokenKind kind)
{
	return current(p) == kind;
}

static bool atItemFirst(parser *p)
{
	return at(p, TOK_FUNC);
}

static bool atRecovery(parser *p)
{
	return atItemFirst(p) || at(p, TOK_LBRACE) || at(p, TOK_RBRACE);
}

static void errorV(parser *p, bool honor_recovery, char *fmt, va_list ap)
{
	span s = { 0 };
	if (atEof(p) || (honor_recovery && atRecovery(p))) {
		p->cursor--;
		span previousTokenSpan = currentSpan(p);
		p->cursor++;

		s.start = previousTokenSpan.end;
		s.end = previousTokenSpan.end + 1;
	} else {
		s = currentSpan(p);
		addToken(p);
	}
	sendDiagnosticToSinkV(DIAG_ERROR, s, fmt, ap);
}

static void error(parser *p, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	errorV(p, true, fmt, ap);
	va_end(ap);
}

static void errorWithoutRecovery(parser *p, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	errorV(p, false, fmt, ap);
	va_end(ap);
}

static void expect(parser *p, tokenKind expected)
{
	tokenKind actual = current(p);
	if (actual == expected) {
		addToken(p);
		return;
	}
	error(p, "expected %s but found %s", showTokenKind(expected),
	      showTokenKind(actual));
}

static identifierId expectIdentifier(parser *p)
{
	identifierId id = { .raw = -1 };
	if (current(p) == TOK_IDENTIFIER)
		id = p->tokens.identifier_ids[p->cursor];
	expect(p, TOK_IDENTIFIER);
	return id;
}

static astExpression *expression(parser *p, memory *m)
{
	astExpression e = { .span.start =
				    atEof(p)
					    ? p->tokens.spans[p->cursor - 1].end
					    : currentSpan(p).start };

	switch (current(p)) {
	case TOK_NUMBER: {
		span span = currentSpan(p);
		expect(p, TOK_NUMBER);

		char *start = (char *)p->content + span.start;
		char *end = NULL;
		u64 value = strtoll(start, &end, 10);

		usize num_bytes_converted = end - start;
		usize token_length = span.end - span.start;
		assert(num_bytes_converted == token_length);

		e.kind = AST_EXPR_INT_LITERAL;
		e.value = value;
		break;
	}

	case TOK_IDENTIFIER: {
		identifierId name = expectIdentifier(p);
		e.kind = AST_EXPR_VARIABLE;
		e.name = name;
		break;
	}

	default:
		error(p, "expected expression");
		e.kind = AST_EXPR_MISSING;
		break;
	}

	e.span.end = p->tokens.spans[p->cursor - 1].end;

	astExpression *ptr = allocateInBump(&m->general, sizeof(astExpression));
	*ptr = e;
	return ptr;
}

static astStatement *statement(parser *p, memory *m)
{
	astStatement s = { .span.start =
				   atEof(p) ? p->tokens.spans[p->cursor - 1].end
					    : currentSpan(p).start };

	switch (current(p)) {
	case TOK_RETURN: {
		expect(p, TOK_RETURN);
		astExpression *value = expression(p, m);
		s.kind = AST_STMT_RETURN;
		s.value = value;
		break;
	}

	case TOK_VAR: {
		expect(p, TOK_VAR);
		identifierId name = expectIdentifier(p);
		expect(p, TOK_EQUAL);
		astExpression *value = expression(p, m);
		s.kind = AST_STMT_LOCAL_DEFINITION;
		s.name = name;
		s.value = value;
		break;
	}

	case TOK_SET: {
		expect(p, TOK_SET);
		astExpression *lhs = expression(p, m);
		expect(p, TOK_EQUAL);
		astExpression *rhs = expression(p, m);
		s.kind = AST_STMT_ASSIGN;
		s.lhs = lhs;
		s.rhs = rhs;
		break;
	}

	case TOK_LBRACE: {
		expect(p, TOK_LBRACE);
		astStatement *statements_start =
			(astStatement *)(m->temp.top + m->temp.bytes_used);
		u32 count = 0;

		bumpMark mark = markBump(&m->temp);

		while (!at(p, TOK_RBRACE) && !atEof(p) && !atItemFirst(p)) {
			astStatement *stmt = statement(p, m);
			astStatement **stmt_ptr = allocateInBump(
				&m->temp, sizeof(astStatement *));
			*stmt_ptr = stmt;
			count++;
		}
		expect(p, TOK_RBRACE);

		astStatement **statements = allocateInBump(
			&m->general, sizeof(astStatement *) * count);
		memcpy(statements, statements_start,
		       sizeof(astStatement *) * count);

		clearBumpToMark(&m->temp, mark);

		s.kind = AST_STMT_BLOCK;
		s.statements = statements;
		s.count = count;
		break;
	}

	default:
		error(p, "expected statement");
		s.kind = AST_STMT_MISSING;
		break;
	}

	s.span.end = p->tokens.spans[p->cursor - 1].end;

	astStatement *ptr = allocateInBump(&m->general, sizeof(astStatement));
	*ptr = s;
	return ptr;
}

static astFunction *function(parser *p, memory *m)
{
	assert(at(p, TOK_FUNC));
	expect(p, TOK_FUNC);

	identifierId name = expectIdentifier(p);
	astStatement *body = statement(p, m);

	astFunction f = {
		.name = name,
		.body = body,
		.next = NULL,
	};
	astFunction *ptr = allocateInBump(&m->general, sizeof(astFunction));
	*ptr = f;
	return ptr;
}

astRoot parse(tokenBuffer tokens, u8 *content, memory *m)
{
	parser p = {
		.tokens = tokens,
		.cursor = 0,
		.content = content,
	};

	astFunction *head = NULL;
	astFunction *current_function = NULL;

	bumpMark mark = markBump(&m->temp);
	while (!atEof(&p)) {
		switch (current(&p)) {
		case TOK_FUNC: {
			astFunction *new_function = function(&p, m);
			if (head == NULL) {
				head = new_function;
				current_function = new_function;
			} else {
				current_function->next = new_function;
				current_function = new_function;
			}
			break;
		}
		default:
			errorWithoutRecovery(&p, "expected function");
			break;
		}
	}
	clearBumpToMark(&m->temp, mark);

	astRoot root = { .functions = head };
	return root;
}

static void newline(u32 indentation)
{
	printf("\n");
	for (u32 i = 0; i < indentation; i++)
		printf("\t");
}

static void debugExpression(astExpression *expression, interner interner)
{
	switch (expression->kind) {
	case AST_EXPR_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_EXPR_INT_LITERAL:
		printf("\033[36m%llu\033[0m", expression->value);
		break;

	case AST_EXPR_VARIABLE:
		if (expression->name.raw == (u32)-1)
			printf("\033[7;31m<missing>\033[0m");
		else
			printf("\033[35m%s\033[0m",
			       lookup(interner, expression->name));
		break;
	}
}

static void debugStatement(astStatement *statement, interner interner,
			   u32 indentation)
{
	switch (statement->kind) {
	case AST_STMT_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_STMT_RETURN:
		printf("\033[32mreturn\033[0m ");
		debugExpression(statement->value, interner);
		break;

	case AST_STMT_LOCAL_DEFINITION:
		printf("\033[32mvar\033[0m ");

		if (statement->name.raw == (u32)-1)
			printf("\033[7;31m<missing>\033[0m");
		else
			printf("\033[35m%s\033[0m",
			       lookup(interner, statement->name));

		printf(" = ");
		debugExpression(statement->value, interner);
		break;

	case AST_STMT_ASSIGN:
		printf("\033[32mset\033[0m ");
		debugExpression(statement->lhs, interner);
		printf(" = ");
		debugExpression(statement->rhs, interner);
		break;

	case AST_STMT_BLOCK:
		if (statement->count == 0) {
			printf("{}");
			break;
		}
		printf("{");
		indentation++;
		for (u32 i = 0; i < statement->count; i++) {
			newline(indentation);
			debugStatement(statement->statements[i], interner,
				       indentation);
		}
		indentation--;
		newline(indentation);
		printf("}");
		break;
	}
}

static void debugFunction(astFunction function, interner interner,
			  u32 indentation)
{
	printf("\033[32mfunc ");
	if (function.name.raw == (u32)-1)
		printf("\033[7;31m<missing>\033[0m");
	else
		printf("\033[33m%s\033[0m", lookup(interner, function.name));

	indentation++;
	newline(indentation);
	debugStatement(function.body, interner, indentation);
	indentation--;
}

void debugAst(astRoot ast, interner interner)
{
	astFunction *f = ast.functions;
	bool first = true;
	u32 indentation = 0;

	while (f != NULL) {
		if (first)
			first = false;
		else
			newline(indentation);

		debugFunction(*f, interner, indentation);
		newline(indentation);
		f = f->next;
	}
}
