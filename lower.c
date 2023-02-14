#include "minic.h"

#define MAX_NODE_COUNT (63 * 1024)
#define MAX_LOCAL_COUNT (63 * 1024)

typedef struct fullNode {
	hirNodeData data;
	hirNodeKind kind;
	hirType type;
	span span;
} fullNode;

static hirLocal lookupLocal(hirRoot *hir, identifierId name)
{
	for (hirLocal local = hir->current_function_locals_start;
	     local.index < hir->local_count; local.index++) {
		if (hirGetLocalName(*hir, local).raw == name.raw)
			return local;
	}

	hirLocal local = { .index = -1 };
	return local;
}

static hirNode allocateNode(hirRoot *hir, fullNode node)
{
	if (hir->node_count >= MAX_NODE_COUNT) {
		sendDiagnosticToSink(DIAG_ERROR, node.span,
				     "reached limit of %u nodes",
				     MAX_NODE_COUNT);
		internalError("ran out of node slots");
	}

	u16 i = hir->node_count;
	hir->node_count++;
	hir->nodes[i] = node.data;
	hir->node_kinds[i] = node.kind;
	hir->node_types[i] = node.type;
	hir->node_spans[i] = node.span;
	hirNode n = { .index = i };
	return n;
}

static hirLocal allocateLocal(hirRoot *hir, identifierId name, hirType type)
{
	assert(hir->local_count < MAX_LOCAL_COUNT);
	u16 i = hir->local_count;
	hir->local_count++;
	hir->local_names[i] = name;
	hir->local_types[i] = type;
	hirLocal l = { .index = i };
	return l;
}

static fullNode lowerExpression(hirRoot *hir, astRoot ast,
				astExpression ast_expression, memory *m)
{
	fullNode n = { .data = { 0 },
		       .kind = -1,
		       .type = -1,
		       .span = astGetExpressionSpan(ast, ast_expression) };

	switch (astGetExpressionKind(ast, ast_expression)) {
	case AST_EXPR_MISSING:
		n.kind = HIR_MISSING;
		n.type = HIR_TYPE_VOID;
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral ast_int_literal =
			astGetExpression(ast, ast_expression).int_literal;
		n.kind = HIR_INT_LITERAL;
		n.type = HIR_TYPE_I64;
		n.data.int_literal.value = ast_int_literal.value;
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable ast_variable =
			astGetExpression(ast, ast_expression).variable;

		if (ast_variable.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal local = lookupLocal(hir, ast_variable.name);
		if (local.index == (u16)-1) {
			sendDiagnosticToSink(
				DIAG_ERROR,
				astGetExpressionSpan(ast, ast_expression),
				"undefined variable");
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		n.kind = HIR_VARIABLE;
		n.type = hirGetLocalType(*hir, local);
		n.data.variable.local = local;
		break;
	}

	case AST_EXPR_BINARY_OPERATION: {
		astBinaryOperation ast_binary_operation =
			astGetExpression(ast, ast_expression).binary_operation;

		hirNode lhs = allocateNode(
			hir,
			lowerExpression(hir, ast, ast_binary_operation.lhs, m));
		hirNode rhs = allocateNode(
			hir,
			lowerExpression(hir, ast, ast_binary_operation.rhs, m));

		n.kind = HIR_BINARY_OPERATION;
		n.type = hirGetNodeType(*hir, lhs);
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

static fullNode lowerStatement(hirRoot *hir, astRoot ast,
			       astStatement ast_statement, memory *m)
{
	fullNode n = { .data = { 0 },
		       .kind = -1,
		       .type = -1,
		       .span = astGetStatementSpan(ast, ast_statement) };

	switch (astGetStatementKind(ast, ast_statement)) {
	case AST_STMT_MISSING:
		n.kind = HIR_MISSING;
		n.type = HIR_TYPE_VOID;
		break;

	case AST_STMT_RETURN: {
		astReturn ast_retrn = astGetStatement(ast, ast_statement).retrn;
		n.kind = HIR_RETURN;
		n.type = HIR_TYPE_VOID;
		n.data.retrn.value = allocateNode(
			hir, lowerExpression(hir, ast, ast_retrn.value, m));
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition ast_local_definition =
			astGetStatement(ast, ast_statement).local_definition;

		hirLocal existing_local =
			lookupLocal(hir, ast_local_definition.name);
		if (existing_local.index != (u16)-1)
			sendDiagnosticToSink(
				DIAG_ERROR,
				astGetStatementSpan(ast, ast_statement),
				"cannot shadow existing variable");

		hirNode rhs = allocateNode(
			hir, lowerExpression(hir, ast,
					     ast_local_definition.value, m));

		if (ast_local_definition.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal local = allocateLocal(hir, ast_local_definition.name,
					       hirGetNodeType(*hir, rhs));

		fullNode lhs_unallocd = {
			.data.variable.local = local,
			.kind = HIR_VARIABLE,
			.type = hirGetLocalType(*hir, local),
		};
		hirNode lhs = allocateNode(hir, lhs_unallocd);

		n.kind = HIR_ASSIGN;
		n.type = HIR_TYPE_VOID;
		n.data.assign.lhs = lhs;
		n.data.assign.rhs = rhs;
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign ast_assign =
			astGetStatement(ast, ast_statement).assign;
		n.kind = HIR_ASSIGN;
		n.type = HIR_TYPE_VOID;
		n.data.assign.lhs = allocateNode(
			hir, lowerExpression(hir, ast, ast_assign.lhs, m));
		n.data.assign.rhs = allocateNode(
			hir, lowerExpression(hir, ast, ast_assign.rhs, m));
		break;
	}

	case AST_STMT_IF: {
		astIf ast_if = astGetStatement(ast, ast_statement).if_;
		n.kind = HIR_IF;
		n.type = HIR_TYPE_VOID;

		n.data.if_.condition = allocateNode(
			hir, lowerExpression(hir, ast, ast_if.condition, m));

		n.data.if_.true_branch = allocateNode(
			hir, lowerStatement(hir, ast, ast_if.true_branch, m));

		if (ast_if.false_branch.index != (u16)-1)
			n.data.if_.false_branch = allocateNode(
				hir, lowerStatement(hir, ast,
						    ast_if.false_branch, m));
		else
			n.data.if_.false_branch.index = -1;

		break;
	}

	case AST_STMT_WHILE: {
		astWhile ast_while = astGetStatement(ast, ast_statement).while_;
		n.kind = HIR_WHILE;
		n.type = HIR_TYPE_VOID;
		n.data.while_.condition = allocateNode(
			hir, lowerExpression(hir, ast, ast_while.condition, m));
		n.data.while_.true_branch = allocateNode(
			hir,
			lowerStatement(hir, ast, ast_while.true_branch, m));
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock ast_block = astGetStatement(ast, ast_statement).block;

		fullNode *nodes_start =
			(fullNode *)(m->temp.top + m->temp.bytes_used);

		bumpMark mark = markBump(&m->temp);

		for (u16 i = 0; i < ast_block.count; i++) {
			astStatement ast_s = { .index = ast_block.start.index +
							i };
			fullNode *ptr =
				allocateInBump(&m->temp, sizeof(fullNode));
			*ptr = lowerStatement(hir, ast, ast_s, m);
		}

		hirNode start = { .index = -1 };

		for (u16 i = 0; i < ast_block.count; i++) {
			hirNode this = allocateNode(hir, nodes_start[i]);
			if (start.index == (u16)-1)
				start = this;
		}

		clearBumpToMark(&m->temp, mark);

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

hirRoot lower(astRoot ast, memory *m)
{
	bumpMark mark = markBump(&m->temp);

	hirRoot hir = {
		.functions =
			(hirFunction *)(m->general.top + m->general.bytes_used),
		.nodes = allocateInBump(&m->temp,
					sizeof(hirNodeData) * MAX_NODE_COUNT),
		.node_kinds = allocateInBump(&m->temp, sizeof(hirNodeKind) *
							       MAX_NODE_COUNT),
		.node_types = allocateInBump(&m->temp,
					     sizeof(hirType) * MAX_NODE_COUNT),
		.node_spans =
			allocateInBump(&m->temp, sizeof(span) * MAX_NODE_COUNT),
		.local_names = allocateInBump(
			&m->temp, sizeof(identifierId) * MAX_LOCAL_COUNT),
		.local_types = allocateInBump(
			&m->temp, sizeof(hirType) * MAX_LOCAL_COUNT),
		.function_count = 0,
		.node_count = 0,
		.local_count = 0,
		.current_function_locals_start = { .index = -1 },
	};

	for (u16 i = 0; i < ast.function_count; i++) {
		astFunction ast_function = ast.functions[i];

		if (ast_function.name.raw == (u32)-1)
			continue;

		hirLocal locals_start = { .index = hir.local_count };
		hir.current_function_locals_start = locals_start;
		hirNode body = allocateNode(
			&hir, lowerStatement(&hir, ast, ast_function.body, m));
		u16 locals_count = hir.local_count - locals_start.index;

		hirFunction function = {
			.locals_start = locals_start,
			.locals_count = locals_count,
			.body = body,
			.name = ast_function.name,
		};
		hirFunction *ptr =
			allocateInBump(&m->general, sizeof(hirFunction));
		*ptr = function;
		hir.function_count++;
	}

	hir.nodes = copyInBump(&m->general, hir.nodes,
			       sizeof(hirNodeData) * hir.node_count);
	hir.node_kinds = copyInBump(&m->general, hir.node_kinds,
				    sizeof(hirNodeKind) * hir.node_count);
	hir.node_types = copyInBump(&m->general, hir.node_kinds,
				    sizeof(hirType) * hir.node_count);
	hir.node_spans = copyInBump(&m->general, hir.node_spans,
				    sizeof(span) * hir.node_count);

	hir.local_names = copyInBump(&m->general, hir.local_names,
				     sizeof(identifierId) * hir.local_count);
	hir.local_types = copyInBump(&m->general, hir.local_types,
				     sizeof(hirType) * hir.local_count);

	clearBumpToMark(&m->temp, mark);

	return hir;
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

u32 typeSize(hirType type)
{
	switch (type) {
	case HIR_TYPE_VOID:
		return 0;
	case HIR_TYPE_I64:
		return 8;
	}
}

typedef struct ctx {
	hirRoot hir;
	interner interner;
	u32 indentation;
} ctx;

static void newline(ctx *c)
{
	printf("\n");
	for (u32 i = 0; i < c->indentation; i++)
		printf("\t");
}

u8 *debugHirType(hirType type)
{
	switch (type) {
	case HIR_TYPE_VOID:
		return (u8 *)"void";
	case HIR_TYPE_I64:
		return (u8 *)"i64";
	}
}

static void debugNode(ctx *c, hirNode node)
{
	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case HIR_INT_LITERAL: {
		hirIntLiteral int_literal =
			hirGetNode(c->hir, node).int_literal;
		printf("\033[36m%llu\033[0m", int_literal.value);
		break;
	}

	case HIR_VARIABLE: {
		hirVariable variable = hirGetNode(c->hir, node).variable;
		identifierId name = hirGetLocalName(c->hir, variable.local);
		printf("\033[35m%s\033[0m", lookup(c->interner, name));
		break;
	}

	case HIR_BINARY_OPERATION: {
		hirBinaryOperation binary_operation =
			hirGetNode(c->hir, node).binary_operation;
		printf("(");
		debugNode(c, binary_operation.lhs);

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

		debugNode(c, binary_operation.rhs);
		printf(")");
		break;
	}

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(c->hir, node).assign;
		printf("\033[32mset\033[0m ");
		debugNode(c, assign.lhs);
		printf(" = ");
		debugNode(c, assign.rhs);
		break;
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(c->hir, node).if_;
		printf("\033[32mif\033[0m ");
		debugNode(c, if_.condition);
		c->indentation++;

		newline(c);
		debugNode(c, if_.true_branch);
		c->indentation--;

		if (if_.false_branch.index != (u16)-1) {
			newline(c);
			printf("\033[32melse\033[0m");
			c->indentation++;

			newline(c);
			debugNode(c, if_.false_branch);
			c->indentation--;
		}
		break;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(c->hir, node).while_;
		printf("\033[32mwhile\033[0m ");
		debugNode(c, while_.condition);
		c->indentation++;
		newline(c);
		debugNode(c, while_.true_branch);
		c->indentation--;
		break;
	}

	case HIR_RETURN: {
		hirReturn retrn = hirGetNode(c->hir, node).retrn;
		printf("\033[32mreturn\033[0m ");
		debugNode(c, retrn.value);
		break;
	}

	case HIR_BLOCK: {
		hirBlock block = hirGetNode(c->hir, node).block;
		if (block.count == 0) {
			printf("{}");
			break;
		}
		printf("{");
		c->indentation++;
		for (u16 i = 0; i < block.count; i++) {
			hirNode n = { .index = block.start.index + i };
			newline(c);
			debugNode(c, n);
		}
		c->indentation--;
		newline(c);
		printf("}");
		break;
	}
	}
}

static void debugFunction(ctx *c, hirFunction function)
{
	printf("\033[32mfunc \033[33m%s\033[0m",
	       lookup(c->interner, function.name));

	c->indentation++;
	for (u16 i = 0; i < function.locals_count; i++) {
		hirLocal local = { .index = function.locals_start.index + i };
		newline(c);
		printf("\033[32mvar \033[35m%s \033[91m%s\033[0m",
		       lookup(c->interner, hirGetLocalName(c->hir, local)),
		       debugHirType(hirGetLocalType(c->hir, local)));
	}

	newline(c);
	debugNode(c, function.body);
	c->indentation--;
}

void debugHir(hirRoot hir, interner interner)
{
	ctx c = {
		.hir = hir,
		.interner = interner,
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
