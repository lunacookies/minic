#include "minic.h"

typedef struct parser {
	tokenBuffer tokens;
	usize cursor;
	u8 *content;
} parser;

static u8 *printTokenKind(tokenKind kind)
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

static token *currentToken(parser *p)
{
	if (atEof(p))
		return NULL;
	return &p->tokens.tokens[p->cursor];
}

static token *previousToken(parser *p)
{
	p->cursor--;
	token *t = currentToken(p);
	p->cursor++;
	return t;
}

static tokenKind current(parser *p)
{
	token *t = currentToken(p);
	if (t == NULL)
		return TOK_EOF;
	return t->kind;
}

static bool at(parser *p, tokenKind kind)
{
	return current(p) == kind;
}

static void errorV(parser *p, char *fmt, va_list ap)
{
	token *t = currentToken(p);
	span s = { 0 };
	if (t == NULL) {
		span previousTokenSpan = previousToken(p)->span;
		s.start = previousTokenSpan.end;
		s.end = previousTokenSpan.end + 1;
	} else
		s = t->span;
	sendDiagnosticToSinkV(DIAG_ERROR, s, fmt, ap);
}

static void error(parser *p, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	errorV(p, fmt, ap);
	va_end(ap);

	if (!atEof(p))
		addToken(p);
}

static void expect(parser *p, tokenKind expected)
{
	tokenKind actual = current(p);
	if (actual == expected) {
		addToken(p);
		return;
	}
	error(p, "expected %s but found %s", printTokenKind(expected),
	      printTokenKind(actual));
}

static u8 *expectCopy(parser *p, tokenKind expected, memory *m)
{
	u8 *ptr = NULL;
	if (current(p) == expected) {
		span span = currentToken(p)->span;
		usize length = span.end - span.start;
		ptr = allocateInBump(&m->general,
				     length + 1); // null-terminated
		memcpy(ptr, p->content + span.start, length);
	}
	expect(p, expected);
	return ptr;
}

static astExpression *expression(parser *p, memory *m)
{
	astExpression e = { 0 };

	switch (current(p)) {
	case TOK_NUMBER: {
		token *tok = currentToken(p);
		expect(p, TOK_NUMBER);

		char *start = (char *)p->content + tok->span.start;
		char *end = NULL;
		u64 value = strtoll(start, &end, 10);

		usize num_bytes_converted = end - start;
		usize token_length = tok->span.end - tok->span.start;
		assert(num_bytes_converted == token_length);

		e.kind = AST_EXPR_INT_LITERAL;
		e.value = value;
		break;
	}

	case TOK_IDENTIFIER: {
		u8 *name = expectCopy(p, TOK_IDENTIFIER, m);
		e.kind = AST_EXPR_VARIABLE;
		e.name = name;
		break;
	}

	default:
		error(p, "expected expression");
		e.kind = AST_EXPR_MISSING;
		break;
	}

	astExpression *ptr = allocateInBump(&m->general, sizeof(astExpression));
	*ptr = e;
	return ptr;
}

static astStatement *statement(parser *p, memory *m)
{
	astStatement s = { 0 };

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
		u8 *name = expectCopy(p, TOK_IDENTIFIER, m);
		expect(p, TOK_EQUAL);
		astExpression *value = expression(p, m);
		s.kind = AST_STMT_LOCAL_DEFINITION;
		s.name = name;
		s.value = value;
		break;
	}

	case TOK_LBRACE: {
		expect(p, TOK_LBRACE);
		astStatement *statements_start =
			(astStatement *)(m->temp.top + m->temp.bytes_used);
		u32 count = 0;

		bumpMark mark = markBump(&m->temp);

		while (!at(p, TOK_RBRACE) && !atEof(p)) {
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

	astStatement *ptr = allocateInBump(&m->general, sizeof(astStatement));
	*ptr = s;
	return ptr;
}

static astFunction *function(parser *p, memory *m)
{
	assert(at(p, TOK_FUNC));
	expect(p, TOK_FUNC);

	u8 *name = expectCopy(p, TOK_IDENTIFIER, m);
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

	clearBump(&m->temp);
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
			error(&p, "expected function");
			break;
		}
	}
	clearBump(&m->temp);

	astRoot root = { .functions = head };
	return root;
}

static void newline(u32 indentation)
{
	printf("\n");
	for (u32 i = 0; i < indentation; i++)
		printf("\t");
}

static void debugExpression(astExpression *expression)
{
	switch (expression->kind) {
	case AST_EXPR_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_EXPR_INT_LITERAL:
		printf("\033[36m%llu\033[0m", expression->value);
		break;

	case AST_EXPR_VARIABLE:
		printf("\033[35m%s\033[0m", expression->name);
		break;
	}
}

static void debugStatement(astStatement *statement, u32 indentation)
{
	switch (statement->kind) {
	case AST_STMT_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_STMT_RETURN:
		printf("\033[32mreturn\033[0m ");
		debugExpression(statement->value);
		break;

	case AST_STMT_LOCAL_DEFINITION:
		printf("\033[32mvar\033[0m \033[35m%s\033[0m = ",
		       statement->name);
		debugExpression(statement->value);
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
			debugStatement(statement->statements[i], indentation);
		}
		indentation--;
		newline(indentation);
		printf("}");
		break;
	}
}

static void debugFunction(astFunction function, u32 indentation)
{
	printf("\033[32mfunc \033[33m%s\033[0m", function.name);
	indentation++;
	newline(indentation);
	debugStatement(function.body, indentation);
	indentation--;
}

void debugAst(astRoot ast)
{
	astFunction *f = ast.functions;
	bool first = true;
	u32 indentation = 0;

	while (f != NULL) {
		if (first)
			first = false;
		else {
			newline(indentation);
			newline(indentation);
		}

		debugFunction(*f, indentation);
		f = f->next;
	}

	newline(indentation);
}
