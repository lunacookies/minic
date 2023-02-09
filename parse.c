#include "minic.h"

#define MAX_EXPRESSION_COUNT (63 * 1024)
#define MAX_STATEMENT_COUNT (63 * 1024)

typedef struct fullExpression {
	astExpressionData data;
	astExpressionKind kind;
	span span;
} fullExpression;

typedef struct fullStatement {
	astStatementData data;
	astStatementKind kind;
	span span;
} fullStatement;

typedef struct parser {
	tokenBuffer tokens;
	usize cursor;
	u8 *content;
	astRoot ast;
} parser;

static astExpression allocateExpression(parser *p, fullExpression expression)
{
	if (p->ast.expression_count >= MAX_EXPRESSION_COUNT) {
		sendDiagnosticToSink(DIAG_ERROR, expression.span,
				     "reached limit of %u expressions",
				     MAX_EXPRESSION_COUNT);
		internalError("ran out of expression slots");
	}

	u16 i = p->ast.expression_count;
	p->ast.expression_count++;
	p->ast.expressions[i] = expression.data;
	p->ast.expression_kinds[i] = expression.kind;
	p->ast.expression_spans[i] = expression.span;
	astExpression e = { .index = i };
	return e;
}

static astStatement allocateStatement(parser *p, fullStatement statement)
{
	if (p->ast.statement_count >= MAX_STATEMENT_COUNT) {
		sendDiagnosticToSink(DIAG_ERROR, statement.span,
				     "reached limit of %u statements",
				     MAX_STATEMENT_COUNT);
		internalError("ran out of statement slots");
	}

	u16 i = p->ast.statement_count;
	p->ast.statement_count++;
	p->ast.statements[i] = statement.data;
	p->ast.statement_kinds[i] = statement.kind;
	p->ast.statement_spans[i] = statement.span;
	astStatement s = { .index = i };
	return s;
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

static fullExpression expression(parser *p, memory *m);

static fullExpression expressionLhs(parser *p, memory *m)
{
	fullExpression e = {
		.data = { 0 },
		.kind = -1,
		.span = { .start = atEof(p) ? p->tokens.spans[p->cursor - 1].end
					    : currentSpan(p).start },
	};

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
		e.data.int_literal.value = value;
		break;
	}

	case TOK_IDENTIFIER: {
		identifierId name = expectIdentifier(p);
		e.kind = AST_EXPR_VARIABLE;
		e.data.variable.name = name;
		break;
	}

	case TOK_LPAREN: {
		expect(p, TOK_LPAREN);
		fullExpression inner = expression(p, m);
		expect(p, TOK_RPAREN);
		return inner;
	}

	default:
		error(p, "expected expression");
		e.kind = AST_EXPR_MISSING;
		break;
	}

	assert(e.kind != (u8)-1);
	e.span.end = p->tokens.spans[p->cursor - 1].end;
	return e;
}

static fullExpression expressionBindingPower(parser *p, u8 min_binding_power,
					     memory *m)
{
	fullExpression lhs = expressionLhs(p, m);

	while (true) {
		if (atEof(p))
			return lhs;

		u8 binding_power = 0;
		astBinaryOperator op = -1;
		switch (current(p)) {
		case TOK_PLUS:
			binding_power = 2;
			op = AST_BINOP_ADD;
			break;
		case TOK_DASH:
			binding_power = 2;
			op = AST_BINOP_SUBTRACT;
			break;
		case TOK_STAR:
			binding_power = 3;
			op = AST_BINOP_MULTIPLY;
			break;
		case TOK_SLASH:
			binding_power = 3;
			op = AST_BINOP_DIVIDE;
			break;
		case TOK_EQUAL_EQUAL:
			binding_power = 1;
			op = AST_BINOP_EQUAL;
			break;
		case TOK_BANG_EQUAL:
			binding_power = 1;
			op = AST_BINOP_NOT_EQUAL;
			break;
		case TOK_LANGLE:
			binding_power = 1;
			op = AST_BINOP_LESS_THAN;
			break;
		case TOK_LANGLE_EQUAL:
			binding_power = 1;
			op = AST_BINOP_LESS_THAN_EQUAL;
			break;
		case TOK_RANGLE:
			binding_power = 1;
			op = AST_BINOP_GREATER_THAN;
			break;
		case TOK_RANGLE_EQUAL:
			binding_power = 1;
			op = AST_BINOP_GREATER_THAN_EQUAL;
			break;
		default:
			return lhs;
		}
		assert(binding_power != 0);
		assert(op != (u8)-1);

		if (binding_power < min_binding_power)
			return lhs;

		// skip past operator token
		addToken(p);

		fullExpression rhs =
			expressionBindingPower(p, binding_power + 1, m);

		astExpression allocd_lhs = allocateExpression(p, lhs);
		astExpression allocd_rhs = allocateExpression(p, rhs);

		fullExpression new_lhs = { .data = {.binary_operation = {
						      .lhs = allocd_lhs,
						      .rhs = allocd_rhs,
								      .op = op,
					      }}, .kind = AST_EXPR_BINARY_OPERATION, .span = {
			.start = astGetExpressionSpan(p->ast, allocd_lhs).start,
			.end = p->tokens.spans[p->cursor - 1].end,
		}, };
		lhs = new_lhs;
	}
}

static fullExpression expression(parser *p, memory *m)
{
	return expressionBindingPower(p, 0, m);
}

static fullStatement statement(parser *p, memory *m)
{
	fullStatement s = {
		.data = { 0 },
		.kind = -1,
		.span = { .start = atEof(p) ? p->tokens.spans[p->cursor - 1].end
					    : currentSpan(p).start },
	};

	switch (current(p)) {
	case TOK_RETURN: {
		expect(p, TOK_RETURN);
		astExpression value = allocateExpression(p, expression(p, m));
		s.kind = AST_STMT_RETURN;
		s.data.retrn.value = value;
		break;
	}

	case TOK_VAR: {
		expect(p, TOK_VAR);
		identifierId name = expectIdentifier(p);
		expect(p, TOK_EQUAL);
		astExpression value = allocateExpression(p, expression(p, m));
		s.kind = AST_STMT_LOCAL_DEFINITION;
		s.data.local_definition.name = name;
		s.data.local_definition.value = value;
		break;
	}

	case TOK_SET: {
		expect(p, TOK_SET);
		astExpression lhs = allocateExpression(p, expression(p, m));
		expect(p, TOK_EQUAL);
		astExpression rhs = allocateExpression(p, expression(p, m));
		s.kind = AST_STMT_ASSIGN;
		s.data.assign.lhs = lhs;
		s.data.assign.rhs = rhs;
		break;
	}

	case TOK_IF: {
		expect(p, TOK_IF);
		astExpression condition =
			allocateExpression(p, expression(p, m));
		astStatement true_branch =
			allocateStatement(p, statement(p, m));
		astStatement false_branch = { .index = -1 };
		if (at(p, TOK_ELSE)) {
			expect(p, TOK_ELSE);
			false_branch = allocateStatement(p, statement(p, m));
		}
		s.kind = AST_STMT_IF;
		s.data.if_.condition = condition;
		s.data.if_.true_branch = true_branch;
		s.data.if_.false_branch = false_branch;
		break;
	}

	case TOK_WHILE: {
		expect(p, TOK_WHILE);
		astExpression condition =
			allocateExpression(p, expression(p, m));
		astStatement body = allocateStatement(p, statement(p, m));
		s.kind = AST_STMT_WHILE;
		s.data.while_.condition = condition;
		s.data.while_.true_branch = body;
		break;
	}

	case TOK_LBRACE: {
		expect(p, TOK_LBRACE);
		fullStatement *statements_start =
			(fullStatement *)(m->temp.top + m->temp.bytes_used);
		u16 count = 0;

		bumpMark mark = markBump(&m->temp);

		while (!at(p, TOK_RBRACE) && !atEof(p) && !atItemFirst(p)) {
			fullStatement stmt = statement(p, m);
			fullStatement *stmt_ptr =
				allocateInBump(&m->temp, sizeof(fullStatement));
			*stmt_ptr = stmt;
			count++;
		}
		expect(p, TOK_RBRACE);

		astStatement start = { .index = -1 };
		astStatement end = { .index = -1 };

		for (u16 i = 0; i < count; i++) {
			astStatement this =
				allocateStatement(p, statements_start[i]);
			if (start.index == (u16)-1)
				start = this;
			end = this;
			end.index++; // inclusive start, exclusive end
		}

		clearBumpToMark(&m->temp, mark);

		s.kind = AST_STMT_BLOCK;
		s.data.block.start = start;
		s.data.block.end = end;
		break;
	}

	default:
		error(p, "expected statement");
		s.kind = AST_STMT_MISSING;
		break;
	}

	assert(s.kind != (u8)-1);
	s.span.end = p->tokens.spans[p->cursor - 1].end;
	return s;
}

static astFunction function(parser *p, memory *m)
{
	assert(at(p, TOK_FUNC));
	expect(p, TOK_FUNC);

	identifierId name = expectIdentifier(p);
	astStatement body = allocateStatement(p, statement(p, m));

	astFunction f = {
		.name = name,
		.body = body,
	};
	return f;
}

astRoot parse(tokenBuffer tokens, u8 *content, memory *m)
{
	bumpMark mark = markBump(&m->temp);

	parser p = {
		.tokens = tokens,
		.cursor = 0,
		.content = content,
		.ast = {
			.functions = (astFunction *)(m->general.top + m->general.bytes_used),
			.statements = allocateInBump(&m->temp, sizeof(astStatementData) * MAX_STATEMENT_COUNT),
			.statement_kinds = allocateInBump(&m->temp, sizeof(astStatementKind) * MAX_STATEMENT_COUNT),
			.statement_spans = allocateInBump(&m->temp, sizeof(span) * MAX_STATEMENT_COUNT),
			.expressions = allocateInBump(&m->temp, sizeof(astStatementData) * MAX_EXPRESSION_COUNT),
			.expression_kinds = allocateInBump(&m->temp, sizeof(astStatementKind) * MAX_EXPRESSION_COUNT),
			.expression_spans = allocateInBump(&m->temp, sizeof(span) * MAX_EXPRESSION_COUNT),
			.function_count = 0,
			.statement_count = 0,
			.expression_count = 0,
		},
	};

	while (!atEof(&p)) {
		switch (current(&p)) {
		case TOK_FUNC: {
			astFunction f = function(&p, m);
			astFunction *ptr = allocateInBump(&m->general,
							  sizeof(astFunction));
			*ptr = f;
			p.ast.function_count++;
			break;
		}
		default:
			errorWithoutRecovery(&p, "expected function");
			break;
		}
	}

	p.ast.statements =
		copyInBump(&m->general, p.ast.statements,
			   sizeof(astStatementData) * p.ast.statement_count);
	p.ast.statement_kinds =
		copyInBump(&m->general, p.ast.statement_kinds,
			   sizeof(astStatementKind) * p.ast.statement_count);
	p.ast.statement_spans =
		copyInBump(&m->general, p.ast.statement_spans,
			   sizeof(span) * p.ast.statement_count);

	p.ast.expressions =
		copyInBump(&m->general, p.ast.expressions,
			   sizeof(astExpressionData) * p.ast.expression_count);
	p.ast.expression_kinds =
		copyInBump(&m->general, p.ast.expression_kinds,
			   sizeof(astExpressionKind) * p.ast.expression_count);
	p.ast.expression_spans =
		copyInBump(&m->general, p.ast.expression_spans,
			   sizeof(span) * p.ast.expression_count);

	clearBumpToMark(&m->temp, mark);

	return p.ast;
}

astStatementData astGetStatement(astRoot ast, astStatement statement)
{
	assert(statement.index < ast.statement_count);
	return ast.statements[statement.index];
}

astStatementKind astGetStatementKind(astRoot ast, astStatement statement)
{
	assert(statement.index < ast.statement_count);
	return ast.statement_kinds[statement.index];
}

span astGetStatementSpan(astRoot ast, astStatement statement)
{
	assert(statement.index < ast.statement_count);
	return ast.statement_spans[statement.index];
}

astExpressionData astGetExpression(astRoot ast, astExpression expression)
{
	assert(expression.index < ast.expression_count);
	return ast.expressions[expression.index];
}

astExpressionKind astGetExpressionKind(astRoot ast, astExpression expression)
{
	assert(expression.index < ast.expression_count);
	return ast.expression_kinds[expression.index];
}

span astGetExpressionSpan(astRoot ast, astExpression expression)
{
	assert(expression.index < ast.expression_count);
	return ast.expression_spans[expression.index];
}

typedef struct ctx {
	astRoot ast;
	interner interner;
	u32 indentation;
} ctx;

static void newline(ctx *c)
{
	printf("\n");
	for (u32 i = 0; i < c->indentation; i++)
		printf("\t");
}

static void debugExpression(ctx *c, astExpression expression)
{
	switch (astGetExpressionKind(c->ast, expression)) {
	case AST_EXPR_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral int_literal =
			astGetExpression(c->ast, expression).int_literal;
		printf("\033[36m%llu\033[0m", int_literal.value);
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable variable =
			astGetExpression(c->ast, expression).variable;
		if (variable.name.raw == (u32)-1)
			printf("\033[7;31m<missing>\033[0m");
		else
			printf("\033[35m%s\033[0m",
			       lookup(c->interner, variable.name));
		break;
	}

	case AST_EXPR_BINARY_OPERATION: {
		astBinaryOperation binary_operation =
			astGetExpression(c->ast, expression).binary_operation;
		printf("(");
		debugExpression(c, binary_operation.lhs);

		switch (binary_operation.op) {
		case AST_BINOP_ADD:
			printf(" + ");
			break;
		case AST_BINOP_SUBTRACT:
			printf(" - ");
			break;
		case AST_BINOP_MULTIPLY:
			printf(" * ");
			break;
		case AST_BINOP_DIVIDE:
			printf(" / ");
			break;
		case AST_BINOP_EQUAL:
			printf(" == ");
			break;
		case AST_BINOP_NOT_EQUAL:
			printf(" != ");
			break;
		case AST_BINOP_LESS_THAN:
			printf(" < ");
			break;
		case AST_BINOP_LESS_THAN_EQUAL:
			printf(" <= ");
			break;
		case AST_BINOP_GREATER_THAN:
			printf(" > ");
			break;
		case AST_BINOP_GREATER_THAN_EQUAL:
			printf(" >= ");
			break;
		}

		debugExpression(c, binary_operation.rhs);
		printf(")");
		break;
	}
	}
}

static void debugStatement(ctx *c, astStatement statement)
{
	switch (astGetStatementKind(c->ast, statement)) {
	case AST_STMT_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case AST_STMT_RETURN: {
		astReturn retrn = astGetStatement(c->ast, statement).retrn;
		printf("\033[32mreturn\033[0m ");
		debugExpression(c, retrn.value);
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition local_definition =
			astGetStatement(c->ast, statement).local_definition;
		printf("\033[32mvar\033[0m ");

		if (local_definition.name.raw == (u32)-1)
			printf("\033[7;31m<missing>\033[0m");
		else
			printf("\033[35m%s\033[0m",
			       lookup(c->interner, local_definition.name));

		printf(" = ");
		debugExpression(c, local_definition.value);
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign assign = astGetStatement(c->ast, statement).assign;
		printf("\033[32mset\033[0m ");
		debugExpression(c, assign.lhs);
		printf(" = ");
		debugExpression(c, assign.rhs);
		break;
	}

	case AST_STMT_IF: {
		astIf if_ = astGetStatement(c->ast, statement).if_;
		printf("\033[32mif\033[0m ");
		debugExpression(c, if_.condition);
		c->indentation++;

		newline(c);
		debugStatement(c, if_.true_branch);
		c->indentation--;

		if (if_.false_branch.index != (u16)-1) {
			newline(c);
			printf("\033[32melse\033[0m");
			c->indentation++;

			newline(c);
			debugStatement(c, if_.false_branch);
			c->indentation--;
		}
		break;
	}

	case AST_STMT_WHILE: {
		astWhile while_ = astGetStatement(c->ast, statement).while_;
		printf("\033[32mwhile\033[0m ");
		debugExpression(c, while_.condition);
		c->indentation++;
		newline(c);
		debugStatement(c, while_.true_branch);
		c->indentation--;
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock block = astGetStatement(c->ast, statement).block;
		bool is_empty = block.start.index == block.end.index;
		if (is_empty) {
			printf("{}");
			break;
		}
		printf("{");
		c->indentation++;
		for (astStatement s = block.start; s.index < block.end.index;
		     s.index++) {
			newline(c);
			debugStatement(c, s);
		}
		c->indentation--;
		newline(c);
		printf("}");
		break;
	}
	}
}

static void debugFunction(ctx *c, astFunction function)
{
	printf("\033[32mfunc ");
	if (function.name.raw == (u32)-1)
		printf("\033[7;31m<missing>\033[0m");
	else
		printf("\033[33m%s\033[0m", lookup(c->interner, function.name));

	c->indentation++;
	newline(c);
	debugStatement(c, function.body);
	c->indentation--;
}

void debugAst(astRoot ast, interner interner)
{
	ctx c = {
		.ast = ast,
		.interner = interner,
		.indentation = 0,
	};

	bool first = true;
	for (u16 i = 0; i < ast.function_count; i++) {
		if (first)
			first = false;
		else
			newline(&c);

		debugFunction(&c, ast.functions[i]);
		newline(&c);
	}
}
