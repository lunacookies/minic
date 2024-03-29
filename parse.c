#include "minic.h"

enum {
	MAX_EXPRESSION_COUNT = 63 * 1024,
	MAX_STATEMENT_COUNT = 63 * 1024,
};

typedef struct fullExpression {
	astExpressionData data;
	span span;
	astExpressionKind kind;
} fullExpression;

typedef struct fullStatement {
	astStatementData data;
	span span;
	astStatementKind kind;
} fullStatement;

typedef struct parser {
	tokenBuffer tokens;
	usize cursor;
	char *content;
	astRoot ast;
	diagnosticsStorage *diagnostics;
} parser;

static astExpression allocateExpression(parser *p, fullExpression expression)
{
	if (p->ast.expression_count >= MAX_EXPRESSION_COUNT) {
		diagnosticsStorageRecord(p->diagnostics, DIAG_ERROR,
					 expression.span,
					 "reached limit of %u expressions",
					 MAX_EXPRESSION_COUNT);
		internalError("ran out of expression slots");
	}

	u16 i = p->ast.expression_count;
	p->ast.expression_count++;
	p->ast.expressions[i] = expression.data;
	p->ast.expression_kinds[i] = expression.kind;
	p->ast.expression_spans[i] = expression.span;
	return astExpressionMake(i);
}

static astStatement allocateStatement(parser *p, fullStatement statement)
{
	if (p->ast.statement_count >= MAX_STATEMENT_COUNT) {
		diagnosticsStorageRecord(
			p->diagnostics, DIAG_ERROR, statement.span,
			"reached limit of %u statements", MAX_STATEMENT_COUNT);
		internalError("ran out of statement slots");
	}

	u16 i = p->ast.statement_count;
	p->ast.statement_count++;
	p->ast.statements[i] = statement.data;
	p->ast.statement_kinds[i] = statement.kind;
	p->ast.statement_spans[i] = statement.span;
	return astStatementMake(i);
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

static bool atStatementFirst(parser *p)
{
	return at(p, TOK_RETURN) || at(p, TOK_SET) || at(p, TOK_IF) ||
	       at(p, TOK_ELSE) || at(p, TOK_WHILE);
}

static bool atRecovery(parser *p)
{
	return atItemFirst(p) || atStatementFirst(p) || at(p, TOK_LBRACE) ||
	       at(p, TOK_RBRACE);
}

typedef enum errorMode {
	ERROR_RECOVER,
	ERROR_EAT_ALL,
	ERROR_EAT_NONE
} errorMode;

static void error(parser *p, errorMode mode, const char *expected_syntax_name)
{
	bool skip_token = false;
	switch (mode) {
	case ERROR_RECOVER:
		skip_token = !atRecovery(p);
		break;

	case ERROR_EAT_ALL:
		skip_token = true;
		break;

	case ERROR_EAT_NONE:
		skip_token = false;
		break;
	}

	// We can’t eat a token if we’re already at EOF!
	if (atEof(p))
		skip_token = false;

	span s = { 0 };
	tokenKind found_token_kind = -1;
	if (skip_token) {
		s = currentSpan(p);
		found_token_kind = current(p);
		addToken(p);
	} else {
		p->cursor--;
		span previousTokenSpan = currentSpan(p);
		p->cursor++;

		s.start = previousTokenSpan.end;
		s.end = previousTokenSpan.end + 1;
	}

	if (skip_token) {
		assert(found_token_kind != (tokenKind)-1);
		diagnosticsStorageRecord(p->diagnostics, DIAG_ERROR, s,
					 "expected %s but found %s",
					 expected_syntax_name,
					 tokenKindShow(found_token_kind));
	} else
		diagnosticsStorageRecord(p->diagnostics, DIAG_ERROR, s,
					 "missing %s", expected_syntax_name);
}

static void expect(parser *p, tokenKind expected, errorMode mode)
{
	tokenKind actual = current(p);
	if (actual == expected) {
		addToken(p);
		return;
	}
	error(p, mode, tokenKindShow(expected));
}

static identifierId expectIdentifier(parser *p, const char *name)
{
	if (current(p) == TOK_IDENTIFIER) {
		identifierId id = p->tokens.identifier_ids[p->cursor];
		addToken(p);
		return id;
	}

	identifierId id = { .raw = -1 };
	error(p, ERROR_RECOVER, name);
	return id;
}

static fullExpression expression(parser *p, const char *error_name, memory *m);

static fullExpression indexExpression(parser *p, fullExpression array,
				      memory *m)
{
	fullExpression e;
	memset(&e, 0, sizeof(e));
	e.kind = AST_EXPR_INDEX;
	e.span = array.span;

	assert(at(p, TOK_LSQUARE));
	expect(p, TOK_LSQUARE, ERROR_RECOVER);
	astExpression index =
		allocateExpression(p, expression(p, "index expression", m));
	expect(p, TOK_RSQUARE, ERROR_RECOVER);

	e.data.index.array = allocateExpression(p, array);
	e.data.index.index = index;

	e.span.end = p->tokens.spans[p->cursor - 1].end;

	return e;
}

static fullExpression expressionLhs(parser *p, const char *error_name,
				    memory *m)
{
	fullExpression e;
	memset(&e, 0, sizeof(e));
	e.kind = -1;
	e.span = (span){ .start = atEof(p) ? p->tokens.spans[p->cursor - 1].end
					   : currentSpan(p).start };

	switch (current(p)) {
	case TOK_NUMBER: {
		span span = currentSpan(p);
		expect(p, TOK_NUMBER, ERROR_RECOVER);

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
		identifierId name = expectIdentifier(p, "variable name");
		e.kind = AST_EXPR_VARIABLE;
		e.data.variable.name = name;
		break;
	}

	case TOK_AMPERSAND: {
		expect(p, TOK_AMPERSAND, ERROR_RECOVER);
		astExpression value = allocateExpression(
			p, expressionLhs(p, "expression", m));
		e.kind = AST_EXPR_ADDRESS_OF;
		e.data.address_of.value = value;
		break;
	}

	case TOK_STAR: {
		expect(p, TOK_STAR, ERROR_RECOVER);
		astExpression value = allocateExpression(
			p, expressionLhs(p, "expression", m));
		e.kind = AST_EXPR_DEREFERENCE;
		e.data.dereference.value = value;
		break;
	}

	case TOK_LPAREN: {
		expect(p, TOK_LPAREN, ERROR_RECOVER);
		fullExpression inner =
			expression(p, "parenthesized expression", m);
		expect(p, TOK_RPAREN, ERROR_RECOVER);
		e = inner;
		break;
	}

	case TOK_LSQUARE: {
		expect(p, TOK_LSQUARE, ERROR_RECOVER);

		bumpMark mark = bumpCreateMark(&m->temp);
		arrayBuilder expressions_builder =
			bumpStartArrayBuilder(&m->temp, sizeof(fullExpression));
		u16 count = 0;

		while (!at(p, TOK_RSQUARE) && !atEof(p) && !atRecovery(p)) {
			fullExpression expr = expression(p, "array element", m);
			if (!at(p, TOK_RSQUARE) && !atEof(p) && !atRecovery(p))
				expect(p, TOK_COMMA, ERROR_EAT_NONE);
			arrayBuilderPush(&expressions_builder, &expr);
			count++;
		}
		expect(p, TOK_RSQUARE, ERROR_RECOVER);

		fullExpression *expressions =
			bumpFinishArrayBuilder(&m->temp, &expressions_builder);

		astExpression start = astExpressionMake(-1);

		for (u16 i = 0; i < count; i++) {
			astExpression this =
				allocateExpression(p, expressions[i]);
			if (start.index == (u16)-1)
				start = this;
		}

		bumpClearToMark(&m->temp, mark);

		e.kind = AST_EXPR_ARRAY_LITERAL;
		e.data.array_literal.start = start;
		e.data.array_literal.count = count;
		break;
	}

	// We don’t want to skip past these.
	case TOK_RPAREN:
	case TOK_RSQUARE:
	case TOK_COMMA:
	case TOK_EQUAL:
		error(p, ERROR_EAT_NONE, error_name);
		e.kind = AST_EXPR_MISSING;
		break;

	default:
		error(p, ERROR_RECOVER, error_name);
		e.kind = AST_EXPR_MISSING;
		break;
	}

	assert(e.kind != (astExpressionKind)-1);
	e.span.end = p->tokens.spans[p->cursor - 1].end;

	for (;;) {
		switch (current(p)) {
		case TOK_LSQUARE:
			e = indexExpression(p, e, m);
			break;

		default:
			return e;
		}
	}
}

static fullExpression expressionBindingPower(parser *p, u8 min_binding_power,
					     const char *error_name, memory *m)
{
	fullExpression lhs = expressionLhs(p, error_name, m);

	for (;;) {
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
		assert(op != (astBinaryOperator)-1);

		if (binding_power < min_binding_power)
			return lhs;

		// skip past operator token
		addToken(p);

		fullExpression rhs = expressionBindingPower(
			p, binding_power + 1, "operand", m);

		astExpression allocd_lhs = allocateExpression(p, lhs);
		astExpression allocd_rhs = allocateExpression(p, rhs);

		span span = {
			.start = astGetExpressionSpan(p->ast, allocd_lhs).start,
			.end = p->tokens.spans[p->cursor - 1].end,
		};

		astBinaryOperation binary_operation;
		memset(&binary_operation, 0, sizeof(binary_operation));
		binary_operation.lhs = allocd_lhs;
		binary_operation.rhs = allocd_rhs;
		binary_operation.op = op;

		fullExpression new_lhs;
		memset(&new_lhs, 0, sizeof(new_lhs));
		new_lhs.data.binary_operation = binary_operation;
		new_lhs.kind = AST_EXPR_BINARY_OPERATION;
		new_lhs.span = span;

		lhs = new_lhs;
	}
}

static fullExpression expression(parser *p, const char *error_name, memory *m)
{
	return expressionBindingPower(p, 0, error_name, m);
}

static fullStatement statement(parser *p, const char *error_name, memory *m);

static fullStatement blockStatement(parser *p, const char *error_name,
				    memory *m)
{
	fullStatement s;
	memset(&s, 0, sizeof(s));
	s.kind = -1;
	s.span = (span){ .start = atEof(p) ? p->tokens.spans[p->cursor - 1].end
					   : currentSpan(p).start };

	if (current(p) != TOK_LBRACE) {
		// We expected a block, but we didn’t get one.
		// Let’s parse a statement, just so we can get the span --
		// we don’t actually keep the parsed statement.

		fullStatement erroneousStatement = statement(p, error_name, m);

		// Only bother the user about this not being a block
		// if we have a working statement in the first place!
		if (erroneousStatement.kind != AST_STMT_MISSING)
			diagnosticsStorageRecord(
				p->diagnostics, DIAG_ERROR,
				erroneousStatement.span,
				"expected block but found a single statement");

		s.kind = AST_STMT_MISSING;
		return s;
	}

	expect(p, TOK_LBRACE, ERROR_RECOVER);

	bumpMark mark = bumpCreateMark(&m->temp);
	arrayBuilder statements_builder =
		bumpStartArrayBuilder(&m->temp, sizeof(fullStatement));
	u16 count = 0;

	while (!at(p, TOK_RBRACE) && !atEof(p) && !atItemFirst(p)) {
		fullStatement stmt = statement(p, "statement", m);
		arrayBuilderPush(&statements_builder, &stmt);
		count++;
	}
	expect(p, TOK_RBRACE, ERROR_RECOVER);

	fullStatement *statements =
		bumpFinishArrayBuilder(&m->temp, &statements_builder);

	astStatement start = astStatementMake(-1);

	for (u16 i = 0; i < count; i++) {
		astStatement this = allocateStatement(p, statements[i]);
		if (start.index == (u16)-1)
			start = this;
	}

	bumpClearToMark(&m->temp, mark);

	s.kind = AST_STMT_BLOCK;
	s.data.block.start = start;
	s.data.block.count = count;

	assert(s.kind != (astStatementKind)-1);
	s.span.end = p->tokens.spans[p->cursor - 1].end;
	return s;
}

static fullStatement statement(parser *p, const char *error_name, memory *m)
{
	fullStatement s;
	memset(&s, 0, sizeof(s));
	s.kind = -1;
	s.span = (span){ .start = atEof(p) ? p->tokens.spans[p->cursor - 1].end
					   : currentSpan(p).start };

	switch (current(p)) {
	case TOK_RETURN: {
		expect(p, TOK_RETURN, ERROR_RECOVER);
		astExpression value =
			allocateExpression(p, expression(p, "return value", m));
		s.kind = AST_STMT_RETURN;
		s.data.retrn.value = value;
		break;
	}

	case TOK_SET: {
		expect(p, TOK_SET, ERROR_RECOVER);
		astExpression lhs = allocateExpression(
			p, expression(p, "left-hand side of assignment", m));
		expect(p, TOK_EQUAL, ERROR_RECOVER);
		astExpression rhs = allocateExpression(
			p, expression(p, "right-hand side of assignment", m));
		s.kind = AST_STMT_ASSIGN;
		s.data.assign.lhs = lhs;
		s.data.assign.rhs = rhs;
		break;
	}

	case TOK_IF: {
		expect(p, TOK_IF, ERROR_RECOVER);
		astExpression condition = allocateExpression(
			p, expression(p, "if statement condition", m));
		astStatement true_block = allocateStatement(
			p, blockStatement(p, "if statement true branch", m));
		astStatement false_block = astStatementMake(-1);
		if (at(p, TOK_ELSE)) {
			expect(p, TOK_ELSE, ERROR_RECOVER);
			false_block = allocateStatement(
				p, blockStatement(
					   p, "if statement false branch", m));
		}
		s.kind = AST_STMT_IF;
		s.data.if_.condition = condition;
		s.data.if_.true_block = true_block;
		s.data.if_.false_block = false_block;
		break;
	}

	case TOK_ELSE: {
		span span = currentSpan(p);
		expect(p, TOK_ELSE, ERROR_RECOVER);
		diagnosticsStorageRecord(p->diagnostics, DIAG_ERROR, span,
					 "unmatched “else”");
		s.kind = AST_STMT_MISSING;
		break;
	}

	case TOK_WHILE: {
		expect(p, TOK_WHILE, ERROR_RECOVER);
		astExpression condition = allocateExpression(
			p, expression(p, "while loop condition", m));
		astStatement body = allocateStatement(
			p, blockStatement(p, "while loop body", m));
		s.kind = AST_STMT_WHILE;
		s.data.while_.condition = condition;
		s.data.while_.true_block = body;
		break;
	}

	case TOK_LBRACE:
		return blockStatement(p, error_name, m);

	case TOK_IDENTIFIER: {
		identifierId name = expectIdentifier(p, "variable name");
		expect(p, TOK_COLON_EQUAL, ERROR_RECOVER);
		astExpression value = allocateExpression(
			p, expression(p, "variable value", m));
		s.kind = AST_STMT_LOCAL_DEFINITION;
		s.data.local_definition.name = name;
		s.data.local_definition.value = value;
		break;
	}

	default:
		error(p, ERROR_RECOVER, error_name);
		s.kind = AST_STMT_MISSING;
		break;
	}

	assert(s.kind != (astStatementKind)-1);
	s.span.end = p->tokens.spans[p->cursor - 1].end;
	return s;
}

static astFunction function(parser *p, memory *m)
{
	assert(at(p, TOK_FUNC));
	expect(p, TOK_FUNC, ERROR_RECOVER);

	identifierId name = expectIdentifier(p, "function name");
	astStatement body =
		allocateStatement(p, statement(p, "function body", m));

	astFunction function;
	memset(&function, 0, sizeof(function));
	function.name = name;
	function.body = body;
	return function;
}

astRoot parse(tokenBuffer tokens, char *content,
	      diagnosticsStorage *diagnostics, memory *m)
{
	bumpMark mark = bumpCreateMark(&m->temp);

	arrayBuilder functions =
		bumpStartArrayBuilder(&m->general, sizeof(astFunction));

	parser p = {
		.tokens = tokens,
		.cursor = 0,
		.content = content,
		.ast = {
			.functions = NULL,
			.statements = bumpAllocateArray(astStatementData, &m->temp, MAX_STATEMENT_COUNT),
			.statement_kinds = bumpAllocateArray(astStatementKind, &m->temp, MAX_STATEMENT_COUNT),
			.statement_spans = bumpAllocateArray(span, &m->temp, MAX_STATEMENT_COUNT),
			.expressions = bumpAllocateArray(astExpressionData, &m->temp, MAX_EXPRESSION_COUNT),
			.expression_kinds = bumpAllocateArray(astExpressionKind, &m->temp, MAX_EXPRESSION_COUNT),
			.expression_spans = bumpAllocateArray(span, &m->temp, MAX_EXPRESSION_COUNT),
			.function_count = 0,
			.statement_count = 0,
			.expression_count = 0,
		},
		.diagnostics = diagnostics,
	};

	while (!atEof(&p)) {
		switch (current(&p)) {
		case TOK_FUNC: {
			astFunction f = function(&p, m);
			arrayBuilderPush(&functions, &f);
			p.ast.function_count++;
			break;
		}
		default:
			error(&p, ERROR_EAT_ALL, "function");
			break;
		}
	}

	p.ast.functions = bumpFinishArrayBuilder(&m->general, &functions);

	p.ast.statements =
		bumpCopyArray(astStatementData, &m->general, p.ast.statements,
			      p.ast.statement_count);
	p.ast.statement_kinds =
		bumpCopyArray(astStatementKind, &m->general,
			      p.ast.statement_kinds, p.ast.statement_count);
	p.ast.statement_spans =
		bumpCopyArray(span, &m->general, p.ast.statement_spans,
			      p.ast.statement_count);

	p.ast.expressions =
		bumpCopyArray(astExpressionData, &m->general, p.ast.expressions,
			      p.ast.expression_count);
	p.ast.expression_kinds =
		bumpCopyArray(astExpressionKind, &m->general,
			      p.ast.expression_kinds, p.ast.expression_count);
	p.ast.expression_spans =
		bumpCopyArray(span, &m->general, p.ast.expression_spans,
			      p.ast.expression_count);

	bumpClearToMark(&m->temp, mark);

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

astExpression astExpressionMake(u16 index)
{
	return (astExpression){ .index = index };
}

astStatement astStatementMake(u16 index)
{
	return (astStatement){ .index = index };
}

typedef struct ctx {
	astRoot ast;
	interner interner;
	stringBuilder *sb;
	u32 indentation;
} ctx;

static void newline(ctx *c)
{
	stringBuilderPrintf(c->sb, "\n");
	for (u32 i = 0; i < c->indentation; i++)
		stringBuilderPrintf(c->sb, "\t");
}

static void debugExpression(ctx *c, astExpression expression)
{
	switch (astGetExpressionKind(c->ast, expression)) {
	case AST_EXPR_MISSING:
		stringBuilderPrintf(c->sb, "<missing>");
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral int_literal =
			astGetExpression(c->ast, expression).int_literal;
		stringBuilderPrintf(c->sb, "%llu", int_literal.value);
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable variable =
			astGetExpression(c->ast, expression).variable;
		if (variable.name.raw == (u32)-1)
			stringBuilderPrintf(c->sb, "<missing>");
		else
			stringBuilderPrintf(
				c->sb, "%s",
				internerLookup(c->interner, variable.name));
		break;
	}

	case AST_EXPR_BINARY_OPERATION: {
		astBinaryOperation binary_operation =
			astGetExpression(c->ast, expression).binary_operation;
		stringBuilderPrintf(c->sb, "(");
		debugExpression(c, binary_operation.lhs);

		switch (binary_operation.op) {
		case AST_BINOP_ADD:
			stringBuilderPrintf(c->sb, " + ");
			break;
		case AST_BINOP_SUBTRACT:
			stringBuilderPrintf(c->sb, " - ");
			break;
		case AST_BINOP_MULTIPLY:
			stringBuilderPrintf(c->sb, " * ");
			break;
		case AST_BINOP_DIVIDE:
			stringBuilderPrintf(c->sb, " / ");
			break;
		case AST_BINOP_EQUAL:
			stringBuilderPrintf(c->sb, " == ");
			break;
		case AST_BINOP_NOT_EQUAL:
			stringBuilderPrintf(c->sb, " != ");
			break;
		case AST_BINOP_LESS_THAN:
			stringBuilderPrintf(c->sb, " < ");
			break;
		case AST_BINOP_LESS_THAN_EQUAL:
			stringBuilderPrintf(c->sb, " <= ");
			break;
		case AST_BINOP_GREATER_THAN:
			stringBuilderPrintf(c->sb, " > ");
			break;
		case AST_BINOP_GREATER_THAN_EQUAL:
			stringBuilderPrintf(c->sb, " >= ");
			break;
		}

		debugExpression(c, binary_operation.rhs);
		stringBuilderPrintf(c->sb, ")");
		break;
	}

	case AST_EXPR_ADDRESS_OF: {
		astAddressOf address_of =
			astGetExpression(c->ast, expression).address_of;
		stringBuilderPrintf(c->sb, "&(");
		debugExpression(c, address_of.value);
		stringBuilderPrintf(c->sb, ")");
		break;
	}

	case AST_EXPR_DEREFERENCE: {
		astDereference dereference =
			astGetExpression(c->ast, expression).dereference;
		stringBuilderPrintf(c->sb, "*(");
		debugExpression(c, dereference.value);
		stringBuilderPrintf(c->sb, ")");
		break;
	}

	case AST_EXPR_INDEX: {
		astIndex index = astGetExpression(c->ast, expression).index;
		stringBuilderPrintf(c->sb, "(");
		debugExpression(c, index.array);
		stringBuilderPrintf(c->sb, ")[");
		debugExpression(c, index.index);
		stringBuilderPrintf(c->sb, "]");
		break;
	}

	case AST_EXPR_ARRAY_LITERAL: {
		astArrayLiteral array_literal =
			astGetExpression(c->ast, expression).array_literal;

		if (array_literal.count == 0) {
			stringBuilderPrintf(c->sb, "[]");
			break;
		}

		if (array_literal.count == 1) {
			stringBuilderPrintf(c->sb, "[");
			debugExpression(c, array_literal.start);
			stringBuilderPrintf(c->sb, "]");
			break;
		}

		stringBuilderPrintf(c->sb, "[");
		c->indentation++;
		for (u16 i = 0; i < array_literal.count; i++) {
			astExpression e = astExpressionMake(
				array_literal.start.index + i);
			newline(c);
			debugExpression(c, e);
			stringBuilderPrintf(c->sb, ",");
		}
		c->indentation--;
		newline(c);
		stringBuilderPrintf(c->sb, "]");
		break;
	}
	}
}

static void debugStatement(ctx *c, astStatement statement)
{
	switch (astGetStatementKind(c->ast, statement)) {
	case AST_STMT_MISSING:
		stringBuilderPrintf(c->sb, "<missing>");
		break;

	case AST_STMT_RETURN: {
		astReturn retrn = astGetStatement(c->ast, statement).retrn;
		stringBuilderPrintf(c->sb, "return ");
		debugExpression(c, retrn.value);
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition local_definition =
			astGetStatement(c->ast, statement).local_definition;

		if (local_definition.name.raw == (u32)-1)
			stringBuilderPrintf(c->sb, "<missing>");
		else
			stringBuilderPrintf(
				c->sb, "%s",
				internerLookup(c->interner,
					       local_definition.name));

		stringBuilderPrintf(c->sb, " := ");
		debugExpression(c, local_definition.value);
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign assign = astGetStatement(c->ast, statement).assign;
		stringBuilderPrintf(c->sb, "set ");
		debugExpression(c, assign.lhs);
		stringBuilderPrintf(c->sb, " = ");
		debugExpression(c, assign.rhs);
		break;
	}

	case AST_STMT_IF: {
		astIf if_ = astGetStatement(c->ast, statement).if_;
		stringBuilderPrintf(c->sb, "if ");
		debugExpression(c, if_.condition);
		stringBuilderPrintf(c->sb, " ");
		debugStatement(c, if_.true_block);

		if (if_.false_block.index == (u16)-1)
			break;

		stringBuilderPrintf(c->sb, " else ");
		debugStatement(c, if_.false_block);
		break;
	}

	case AST_STMT_WHILE: {
		astWhile while_ = astGetStatement(c->ast, statement).while_;
		stringBuilderPrintf(c->sb, "while ");
		debugExpression(c, while_.condition);
		stringBuilderPrintf(c->sb, " ");
		debugStatement(c, while_.true_block);
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock block = astGetStatement(c->ast, statement).block;
		if (block.count == 0) {
			stringBuilderPrintf(c->sb, "{}");
			break;
		}
		stringBuilderPrintf(c->sb, "{");
		c->indentation++;
		for (u16 i = 0; i < block.count; i++) {
			astStatement s =
				astStatementMake(block.start.index + i);
			newline(c);
			debugStatement(c, s);
		}
		c->indentation--;
		newline(c);
		stringBuilderPrintf(c->sb, "}");
		break;
	}
	}
}

static void debugFunction(ctx *c, astFunction function)
{
	stringBuilderPrintf(c->sb, "func ");
	if (function.name.raw == (u32)-1)
		stringBuilderPrintf(c->sb, "<missing>");
	else
		stringBuilderPrintf(c->sb, "%s",
				    internerLookup(c->interner, function.name));

	if (astGetStatementKind(c->ast, function.body) == AST_STMT_BLOCK) {
		stringBuilderPrintf(c->sb, " ");
		debugStatement(c, function.body);
	} else {
		c->indentation++;
		newline(c);
		debugStatement(c, function.body);
		c->indentation--;
	}
}

void astDebug(astRoot ast, interner interner, stringBuilder *sb)
{
	ctx c = {
		.ast = ast,
		.interner = interner,
		.sb = sb,
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

void astDebugPrint(astRoot ast, interner interner, bump *b)
{
	bumpMark mark = bumpCreateMark(b);
	stringBuilder sb = stringBuilderCreate(b);
	astDebug(ast, interner, &sb);
	printf("%s", stringBuilderFinish(sb));
	bumpClearToMark(b, mark);
}

char *parseTests(char *input, memory *m)
{
	diagnosticsStorage diagnostics = diagnosticsStorageCreate(&m->general);
	tokenBuffer buf = lex(input, &diagnostics, m);
	interner interner = intern(&buf, &input, 1, m);
	astRoot ast = parse(buf, input, &diagnostics, m);
	stringBuilder sb = stringBuilderCreate(&m->temp);
	astDebug(ast, interner, &sb);
	diagnosticsStorageDebug(diagnostics, &sb);
	return stringBuilderFinish(sb);
}
