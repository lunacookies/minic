#include "minic.h"

enum {
	MAX_NODE_COUNT = 63 * 1024,
	MAX_LOCAL_COUNT = 63 * 1024,
};

typedef struct fullNode {
	hirNodeData data;
	hirNodeKind kind;
	hirType type;
	span span;
} fullNode;

typedef struct ctx {
	hirRoot hir;
	astRoot ast;
	diagnosticsStorage *diagnostics;
} ctx;

static hirLocal lookupLocal(ctx *c, identifierId name)
{
	for (hirLocal local = c->hir.current_function_locals_start;
	     local.index < c->hir.local_count; local.index++) {
		if (hirGetLocalName(c->hir, local).raw == name.raw)
			return local;
	}

	return (hirLocal){ .index = -1 };
}

static hirNode allocateNode(ctx *c, fullNode node)
{
	if (c->hir.node_count >= MAX_NODE_COUNT) {
		diagnosticsStorageRecord(c->diagnostics, DIAG_ERROR, node.span,
					 "reached limit of %u nodes",
					 MAX_NODE_COUNT);
		internalError("ran out of node slots");
	}

	u16 i = c->hir.node_count;
	c->hir.node_count++;
	c->hir.nodes[i] = node.data;
	c->hir.node_kinds[i] = node.kind;
	c->hir.node_types[i] = node.type;
	c->hir.node_spans[i] = node.span;
	return (hirNode){ .index = i };
}

static hirLocal allocateLocal(ctx *c, identifierId name, hirType type)
{
	assert(c->hir.local_count < MAX_LOCAL_COUNT);
	u16 i = c->hir.local_count;
	c->hir.local_count++;
	c->hir.local_names[i] = name;
	c->hir.local_types[i] = type;
	return (hirLocal){ .index = i };
}

static fullNode lowerExpression(ctx *c, astExpression ast_expression, memory *m)
{
	fullNode n = {
		.data = { 0 },
		.kind = -1,
		.type = -1,
		.span = astGetExpressionSpan(c->ast, ast_expression),
	};

	switch (astGetExpressionKind(c->ast, ast_expression)) {
	case AST_EXPR_MISSING:
		n.kind = HIR_MISSING;
		n.type = HIR_TYPE_VOID;
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral ast_int_literal =
			astGetExpression(c->ast, ast_expression).int_literal;
		n.kind = HIR_INT_LITERAL;
		n.type = HIR_TYPE_I64;
		n.data.int_literal.value = ast_int_literal.value;
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable ast_variable =
			astGetExpression(c->ast, ast_expression).variable;

		if (ast_variable.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal local = lookupLocal(c, ast_variable.name);
		if (local.index == (u16)-1) {
			diagnosticsStorageRecord(
				c->diagnostics, DIAG_ERROR,
				astGetExpressionSpan(c->ast, ast_expression),
				"undefined variable");
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		n.kind = HIR_VARIABLE;
		n.type = hirGetLocalType(c->hir, local);
		n.data.variable.local = local;
		break;
	}

	case AST_EXPR_BINARY_OPERATION: {
		astBinaryOperation ast_binary_operation =
			astGetExpression(c->ast, ast_expression)
				.binary_operation;

		hirNode lhs = allocateNode(
			c, lowerExpression(c, ast_binary_operation.lhs, m));
		hirNode rhs = allocateNode(
			c, lowerExpression(c, ast_binary_operation.rhs, m));

		n.kind = HIR_BINARY_OPERATION;
		n.type = hirGetNodeType(c->hir, lhs);
		n.data.binary_operation.lhs = lhs;
		n.data.binary_operation.rhs = rhs;
		n.data.binary_operation.op = ast_binary_operation.op;
		break;
	}
	}

	assert(n.kind != (hirNodeKind)-1);
	assert(n.type != (hirType)-1);
	return n;
}

static fullNode lowerStatement(ctx *c, astStatement ast_statement, memory *m)
{
	fullNode n = {
		.data = { 0 },
		.kind = -1,
		.type = -1,
		.span = astGetStatementSpan(c->ast, ast_statement),
	};

	switch (astGetStatementKind(c->ast, ast_statement)) {
	case AST_STMT_MISSING:
		n.kind = HIR_MISSING;
		n.type = HIR_TYPE_VOID;
		break;

	case AST_STMT_RETURN: {
		astReturn ast_retrn =
			astGetStatement(c->ast, ast_statement).retrn;
		n.kind = HIR_RETURN;
		n.type = HIR_TYPE_VOID;
		n.data.retrn.value =
			allocateNode(c, lowerExpression(c, ast_retrn.value, m));
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition ast_local_definition =
			astGetStatement(c->ast, ast_statement).local_definition;

		hirLocal existing_local =
			lookupLocal(c, ast_local_definition.name);
		if (existing_local.index != (u16)-1)
			diagnosticsStorageRecord(
				c->diagnostics, DIAG_ERROR,
				astGetStatementSpan(c->ast, ast_statement),
				"cannot shadow existing variable");

		hirNode rhs = allocateNode(
			c, lowerExpression(c, ast_local_definition.value, m));

		if (ast_local_definition.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal local = allocateLocal(c, ast_local_definition.name,
					       hirGetNodeType(c->hir, rhs));

		fullNode lhs_unallocd = {
			.data.variable.local = local,
			.kind = HIR_VARIABLE,
			.type = hirGetLocalType(c->hir, local),
		};
		hirNode lhs = allocateNode(c, lhs_unallocd);

		n.kind = HIR_ASSIGN;
		n.type = HIR_TYPE_VOID;
		n.data.assign.lhs = lhs;
		n.data.assign.rhs = rhs;
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign ast_assign =
			astGetStatement(c->ast, ast_statement).assign;
		n.kind = HIR_ASSIGN;
		n.type = HIR_TYPE_VOID;
		n.data.assign.lhs =
			allocateNode(c, lowerExpression(c, ast_assign.lhs, m));
		n.data.assign.rhs =
			allocateNode(c, lowerExpression(c, ast_assign.rhs, m));
		break;
	}

	case AST_STMT_IF: {
		astIf ast_if = astGetStatement(c->ast, ast_statement).if_;
		n.kind = HIR_IF;
		n.type = HIR_TYPE_VOID;

		n.data.if_.condition = allocateNode(
			c, lowerExpression(c, ast_if.condition, m));

		n.data.if_.true_branch = allocateNode(
			c, lowerStatement(c, ast_if.true_branch, m));

		if (ast_if.false_branch.index != (u16)-1)
			n.data.if_.false_branch = allocateNode(
				c, lowerStatement(c, ast_if.false_branch, m));
		else
			n.data.if_.false_branch.index = -1;

		break;
	}

	case AST_STMT_WHILE: {
		astWhile ast_while =
			astGetStatement(c->ast, ast_statement).while_;
		n.kind = HIR_WHILE;
		n.type = HIR_TYPE_VOID;
		n.data.while_.condition = allocateNode(
			c, lowerExpression(c, ast_while.condition, m));
		n.data.while_.true_branch = allocateNode(
			c, lowerStatement(c, ast_while.true_branch, m));
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock ast_block =
			astGetStatement(c->ast, ast_statement).block;

		fullNode *nodes_start =
			(fullNode *)(m->temp.top + m->temp.bytes_used);

		bumpMark mark = bumpCreateMark(&m->temp);

		for (u16 i = 0; i < ast_block.count; i++) {
			astStatement ast_s = { .index = ast_block.start.index +
							i };
			fullNode *ptr =
				bumpAllocate(&m->temp, sizeof(fullNode));
			*ptr = lowerStatement(c, ast_s, m);
		}

		hirNode start = { .index = -1 };

		for (u16 i = 0; i < ast_block.count; i++) {
			hirNode this = allocateNode(c, nodes_start[i]);
			if (start.index == (u16)-1)
				start = this;
		}

		bumpClearToMark(&m->temp, mark);

		n.kind = HIR_BLOCK;
		n.type = HIR_TYPE_VOID;
		n.data.block.start = start;
		n.data.block.count = ast_block.count;
		break;
	}
	}

	assert(n.kind != (hirNodeKind)-1);
	assert(n.type != (hirType)-1);
	return n;
}

hirRoot lower(astRoot ast, diagnosticsStorage *diagnostics, memory *m)
{
	bumpMark mark = bumpCreateMark(&m->temp);

	ctx c = {
		.hir = {
			.functions = (hirFunction *)(m->general.top + m->general.bytes_used),
			.nodes = bumpAllocate(&m->temp, sizeof(hirNodeData) * MAX_NODE_COUNT),
			.node_kinds = bumpAllocate(&m->temp, sizeof(hirNodeKind) * MAX_NODE_COUNT),
			.node_types = bumpAllocate(&m->temp, sizeof(hirType) * MAX_NODE_COUNT),
			.node_spans = bumpAllocate(&m->temp, sizeof(span) * MAX_NODE_COUNT),
			.local_names = bumpAllocate(&m->temp, sizeof(identifierId) * MAX_LOCAL_COUNT),
			.local_types = bumpAllocate(&m->temp, sizeof(hirType) * MAX_LOCAL_COUNT),
			.function_count = 0,
			.node_count = 0,
			.local_count = 0,
			.current_function_locals_start = { .index = -1 },
		},
		.ast = ast,
		.diagnostics = diagnostics,
	};

	for (u16 i = 0; i < c.ast.function_count; i++) {
		astFunction ast_function = c.ast.functions[i];

		if (ast_function.name.raw == (u32)-1)
			continue;

		hirLocal locals_start = { .index = c.hir.local_count };
		c.hir.current_function_locals_start = locals_start;
		hirNode body = allocateNode(
			&c, lowerStatement(&c, ast_function.body, m));
		u16 locals_count = c.hir.local_count - locals_start.index;

		hirFunction function = {
			.locals_start = locals_start,
			.locals_count = locals_count,
			.body = body,
			.name = ast_function.name,
		};
		hirFunction *ptr =
			bumpAllocate(&m->general, sizeof(hirFunction));
		*ptr = function;
		c.hir.function_count++;
	}

	c.hir.nodes = bumpCopy(&m->general, c.hir.nodes,
			       sizeof(hirNodeData) * c.hir.node_count);
	c.hir.node_kinds = bumpCopy(&m->general, c.hir.node_kinds,
				    sizeof(hirNodeKind) * c.hir.node_count);
	c.hir.node_types = bumpCopy(&m->general, c.hir.node_kinds,
				    sizeof(hirType) * c.hir.node_count);
	c.hir.node_spans = bumpCopy(&m->general, c.hir.node_spans,
				    sizeof(span) * c.hir.node_count);

	c.hir.local_names = bumpCopy(&m->general, c.hir.local_names,
				     sizeof(identifierId) * c.hir.local_count);
	c.hir.local_types = bumpCopy(&m->general, c.hir.local_types,
				     sizeof(hirType) * c.hir.local_count);

	bumpClearToMark(&m->temp, mark);

	return c.hir;
}

hirNodeData hirGetNode(hirRoot hir, hirNode node)
{
	assert(node.index < hir.node_count);
	return hir.nodes[node.index];
}

hirNodeKind hirGetNodeKind(hirRoot hir, hirNode node)
{
	assert(node.index < hir.node_count);
	return hir.node_kinds[node.index];
}

hirType hirGetNodeType(hirRoot hir, hirNode node)
{
	assert(node.index < hir.node_count);
	return hir.node_types[node.index];
}

span hirGetNodeSpan(hirRoot hir, hirNode node)
{
	assert(node.index < hir.node_count);
	return hir.node_spans[node.index];
}

identifierId hirGetLocalName(hirRoot hir, hirLocal local)
{
	assert(local.index < hir.local_count);
	return hir.local_names[local.index];
}

hirType hirGetLocalType(hirRoot hir, hirLocal local)
{
	assert(local.index < hir.local_count);
	return hir.local_types[local.index];
}

u32 hirTypeSize(hirType type)
{
	switch (type) {
	case HIR_TYPE_VOID:
		return 0;
	case HIR_TYPE_I64:
		return 8;
	}
}

typedef struct debugCtx {
	hirRoot hir;
	interner interner;
	stringBuilder *sb;
	u32 indentation;
} debugCtx;

static void newline(debugCtx *c)
{
	stringBuilderPrintf(c->sb, "\n");
	for (u32 i = 0; i < c->indentation; i++)
		stringBuilderPrintf(c->sb, "\t");
}

u8 *hirTypeDebug(hirType type)
{
	switch (type) {
	case HIR_TYPE_VOID:
		return (u8 *)"void";
	case HIR_TYPE_I64:
		return (u8 *)"i64";
	}
}

static void debugNode(debugCtx *c, hirNode node)
{
	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_MISSING:
		stringBuilderPrintf(c->sb, "<missing>");
		break;

	case HIR_INT_LITERAL: {
		hirIntLiteral int_literal =
			hirGetNode(c->hir, node).int_literal;
		stringBuilderPrintf(c->sb, "%llu", int_literal.value);
		break;
	}

	case HIR_VARIABLE: {
		hirVariable variable = hirGetNode(c->hir, node).variable;
		identifierId name = hirGetLocalName(c->hir, variable.local);
		stringBuilderPrintf(c->sb, "%s",
				    internerLookup(c->interner, name));
		break;
	}

	case HIR_BINARY_OPERATION: {
		hirBinaryOperation binary_operation =
			hirGetNode(c->hir, node).binary_operation;
		stringBuilderPrintf(c->sb, "(");
		debugNode(c, binary_operation.lhs);

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

		debugNode(c, binary_operation.rhs);
		stringBuilderPrintf(c->sb, ")");
		break;
	}

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(c->hir, node).assign;
		stringBuilderPrintf(c->sb, "set ");
		debugNode(c, assign.lhs);
		stringBuilderPrintf(c->sb, " = ");
		debugNode(c, assign.rhs);
		break;
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(c->hir, node).if_;
		stringBuilderPrintf(c->sb, "if ");
		debugNode(c, if_.condition);

		if (hirGetNodeKind(c->hir, if_.true_branch) == HIR_BLOCK) {
			stringBuilderPrintf(c->sb, " ");
			debugNode(c, if_.true_branch);
			stringBuilderPrintf(c->sb, " ");
		} else {
			c->indentation++;
			newline(c);
			debugNode(c, if_.true_branch);
			c->indentation--;
			newline(c);
		}

		if (if_.false_branch.index == (u16)-1)
			break;

		stringBuilderPrintf(c->sb, "else");

		if (hirGetNodeKind(c->hir, if_.false_branch) == HIR_BLOCK) {
			stringBuilderPrintf(c->sb, " ");
			debugNode(c, if_.false_branch);
		} else {
			c->indentation++;
			newline(c);
			debugNode(c, if_.false_branch);
			c->indentation--;
		}
		break;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(c->hir, node).while_;
		stringBuilderPrintf(c->sb, "while ");
		debugNode(c, while_.condition);

		if (hirGetNodeKind(c->hir, while_.true_branch) == HIR_BLOCK) {
			stringBuilderPrintf(c->sb, " ");
			debugNode(c, while_.true_branch);
		} else {
			c->indentation++;
			newline(c);
			debugNode(c, while_.true_branch);
			c->indentation--;
		}
		break;
	}

	case HIR_RETURN: {
		hirReturn retrn = hirGetNode(c->hir, node).retrn;
		stringBuilderPrintf(c->sb, "return ");
		debugNode(c, retrn.value);
		break;
	}

	case HIR_BLOCK: {
		hirBlock block = hirGetNode(c->hir, node).block;
		if (block.count == 0) {
			stringBuilderPrintf(c->sb, "{}");
			break;
		}
		stringBuilderPrintf(c->sb, "{");
		c->indentation++;
		for (u16 i = 0; i < block.count; i++) {
			hirNode n = { .index = block.start.index + i };
			newline(c);
			debugNode(c, n);
		}
		c->indentation--;
		newline(c);
		stringBuilderPrintf(c->sb, "}");
		break;
	}
	}
}

static void debugFunction(debugCtx *c, hirFunction function)
{
	stringBuilderPrintf(c->sb, "func %s",
			    internerLookup(c->interner, function.name));

	c->indentation++;
	for (u16 i = 0; i < function.locals_count; i++) {
		hirLocal local = { .index = function.locals_start.index + i };
		newline(c);
		stringBuilderPrintf(
			c->sb, "var %s %s",
			internerLookup(c->interner,
				       hirGetLocalName(c->hir, local)),
			hirTypeDebug(hirGetLocalType(c->hir, local)));
	}

	newline(c);
	debugNode(c, function.body);
	c->indentation--;
}

void hirDebug(hirRoot hir, interner interner, stringBuilder *sb)
{
	debugCtx c = {
		.hir = hir,
		.interner = interner,
		.sb = sb,
		.indentation = 0,
	};

	bool first = true;
	for (u16 i = 0; i < hir.function_count; i++) {
		if (first)
			first = false;
		else
			newline(&c);

		debugFunction(&c, hir.functions[i]);
		newline(&c);
	}
}

void hirDebugPrint(hirRoot hir, interner interner, bump *b)
{
	bumpMark mark = bumpCreateMark(b);
	stringBuilder sb = stringBuilderCreate(b);
	hirDebug(hir, interner, &sb);
	printf("%s", stringBuilderFinish(sb));
	bumpClearToMark(b, mark);
}
