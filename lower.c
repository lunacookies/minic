#include "minic.h"

enum {
	MAX_NODE_COUNT = 63 * 1024,
	MAX_LOCAL_COUNT = 63 * 1024,
	MAX_TYPE_COUNT = 63 * 1024,
	SEEN_TYPES_SLOT_COUNT = MAX_TYPE_COUNT * 4 / 3, // max 0.75 load factor
	LOCALS_BY_NAME_SLOT_COUNT = MAX_LOCAL_COUNT * 4 / 3,
};

typedef struct fullNode {
	hirNodeData data;
	span span;
	hirType type;
	hirNodeKind kind;
	u8 pad[5];
} fullNode;

typedef struct fullType {
	hirTypeData data;
	hirTypeKind kind;
	u8 pad;
} fullType;

typedef struct ctx {
	hirRoot hir;
	astRoot ast;
	diagnosticsStorage *diagnostics;
	map locals_by_name;
	map seen_types;
} ctx;

static hirLocal lookupLocal(ctx *c, identifierId name)
{
	hirLocal *local =
		mapLookup(identifierId, hirLocal, &c->locals_by_name, &name);
	if (local == NULL)
		return (hirLocal){ .index = -1 };
	return *local;
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

static hirLocal allocateLocal(ctx *c, identifierId name, hirType type,
			      span span)
{
	assert(c->hir.local_count < MAX_LOCAL_COUNT);

	u16 i = c->hir.local_count;
	c->hir.local_count++;

	hirLocal local = { .index = i };
	mapInsert(identifierId, hirLocal, &c->locals_by_name, &name, &local);

	c->hir.local_names[i] = name;
	c->hir.local_types[i] = type;
	c->hir.local_spans[i] = span;
	return local;
}

static hirType allocateType(ctx *c, hirTypeKind kind, hirTypeData data)
{
	assert(c->hir.type_count < MAX_TYPE_COUNT);

	fullType full_type = { .kind = kind, .data = data };
	hirType *lookup_result =
		mapLookup(fullType, hirType, &c->seen_types, &full_type);

	if (lookup_result == NULL) {
		u16 i = c->hir.type_count;
		hirType type = { .index = i };
		c->hir.type_count++;
		c->hir.types[i] = data;
		c->hir.type_kinds[i] = kind;

		mapInsert(fullType, hirType, &c->seen_types, &full_type, &type);
		return type;
	}

	return *lookup_result;
}

static fullNode lowerExpression(ctx *c, astExpression ast_expression, memory *m)
{
	fullNode n = {
		.data = { 0 },
		.kind = -1,
		.type = { .index = -1 },
		.span = astGetExpressionSpan(c->ast, ast_expression),
	};

	switch (astGetExpressionKind(c->ast, ast_expression)) {
	case AST_EXPR_MISSING:
		n.kind = HIR_MISSING;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral ast_int_literal =
			astGetExpression(c->ast, ast_expression).int_literal;
		n.kind = HIR_INT_LITERAL;
		n.type = allocateType(c, HIR_TYPE_I64, (hirTypeData){ 0 });
		n.data.int_literal.value = ast_int_literal.value;
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable ast_variable =
			astGetExpression(c->ast, ast_expression).variable;

		if (ast_variable.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
			break;
		}

		hirLocal local = lookupLocal(c, ast_variable.name);
		if (local.index == (u16)-1) {
			diagnosticsStorageRecord(
				c->diagnostics, DIAG_ERROR,
				astGetExpressionSpan(c->ast, ast_expression),
				"undefined variable");
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
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

	case AST_EXPR_ADDRESS_OF: {
		astAddressOf ast_address_of =
			astGetExpression(c->ast, ast_expression).address_of;

		hirNode value = allocateNode(
			c, lowerExpression(c, ast_address_of.value, m));

		hirPointer type = {
			.child_type = hirGetNodeType(c->hir, value),
		};

		n.kind = HIR_ADDRESS_OF;
		n.type = allocateType(c, HIR_TYPE_POINTER,
				      (hirTypeData){ .pointer = type });
		n.data.address_of.value = value;
		break;
	}

	case AST_EXPR_DEREFERENCE: {
		astDereference ast_dereference =
			astGetExpression(c->ast, ast_expression).dereference;

		hirNode value = allocateNode(
			c, lowerExpression(c, ast_dereference.value, m));

		hirType value_type = hirGetNodeType(c->hir, value);
		if (hirGetTypeKind(c->hir, value_type) != HIR_TYPE_POINTER) {
			u8 buffer[128];
			bump b = bumpCreate(buffer, sizeof(buffer));
			stringBuilder sb = stringBuilderCreate(&b);

			stringBuilderPrintf(
				&sb, "cannot dereference non-pointer type “");
			hirTypeShow(c->hir, value_type, &sb);
			stringBuilderPrintf(&sb, "”");

			char *message = stringBuilderFinish(sb);
			diagnosticsStorageRecord(c->diagnostics, DIAG_ERROR,
						 hirGetNodeSpan(c->hir, value),
						 message);
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
			break;
		}

		hirType child_type =
			hirGetType(c->hir, value_type).pointer.child_type;

		n.kind = HIR_DEREFERENCE;
		n.type = child_type;
		n.data.dereference.value = value;
		break;
	}
	}

	assert(n.kind != (hirNodeKind)-1);
	assert(n.type.index != (u16)-1);
	return n;
}

static fullNode lowerStatement(ctx *c, astStatement ast_statement, memory *m)
{
	fullNode n = {
		.data = { 0 },
		.kind = -1,
		.type = { .index = -1 },
		.span = astGetStatementSpan(c->ast, ast_statement),
	};

	switch (astGetStatementKind(c->ast, ast_statement)) {
	case AST_STMT_MISSING:
		n.kind = HIR_MISSING;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		break;

	case AST_STMT_RETURN: {
		astReturn ast_retrn =
			astGetStatement(c->ast, ast_statement).retrn;
		n.kind = HIR_RETURN;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		n.data.retrn.value =
			allocateNode(c, lowerExpression(c, ast_retrn.value, m));
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition ast_local_definition =
			astGetStatement(c->ast, ast_statement).local_definition;

		hirLocal existing_local =
			lookupLocal(c, ast_local_definition.name);
		if (existing_local.index != (u16)-1) {
			diagnosticsStorageRecord(
				c->diagnostics, DIAG_ERROR,
				astGetStatementSpan(c->ast, ast_statement),
				"cannot shadow existing variable");
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
			break;
		}

		hirNode rhs = allocateNode(
			c, lowerExpression(c, ast_local_definition.value, m));

		if (ast_local_definition.name.raw == (u32)-1) {
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
			break;
		}

		hirLocal local = allocateLocal(
			c, ast_local_definition.name,
			hirGetNodeType(c->hir, rhs),
			astGetStatementSpan(c->ast, ast_statement));

		fullNode lhs_unallocd = {
			.data.variable.local = local,
			.kind = HIR_VARIABLE,
			.type = hirGetLocalType(c->hir, local),
		};
		hirNode lhs = allocateNode(c, lhs_unallocd);

		n.kind = HIR_ASSIGN;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		n.data.assign.lhs = lhs;
		n.data.assign.rhs = rhs;
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign ast_assign =
			astGetStatement(c->ast, ast_statement).assign;

		fullNode lhs = lowerExpression(c, ast_assign.lhs, m);
		fullNode rhs = lowerExpression(c, ast_assign.rhs, m);

		if (lhs.type.index != rhs.type.index) {
			u8 buffer[128];
			bump b = bumpCreate(buffer, sizeof(buffer));
			stringBuilder sb = stringBuilderCreate(&b);

			stringBuilderPrintf(&sb, "expected “");
			hirTypeShow(c->hir, lhs.type, &sb);
			stringBuilderPrintf(&sb, "” but found “");
			hirTypeShow(c->hir, rhs.type, &sb);
			stringBuilderPrintf(&sb, "”");

			char *message = stringBuilderFinish(sb);
			diagnosticsStorageRecord(c->diagnostics, DIAG_ERROR,
						 rhs.span, message);
			n.kind = HIR_MISSING;
			n.type = allocateType(c, HIR_TYPE_VOID,
					      (hirTypeData){ 0 });
			break;
		}

		n.kind = HIR_ASSIGN;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		n.data.assign.lhs = allocateNode(c, lhs);
		n.data.assign.rhs = allocateNode(c, rhs);
		break;
	}

	case AST_STMT_IF: {
		astIf ast_if = astGetStatement(c->ast, ast_statement).if_;
		n.kind = HIR_IF;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });

		n.data.if_.condition = allocateNode(
			c, lowerExpression(c, ast_if.condition, m));

		n.data.if_.true_block = allocateNode(
			c, lowerStatement(c, ast_if.true_block, m));

		if (ast_if.false_block.index != (u16)-1)
			n.data.if_.false_block = allocateNode(
				c, lowerStatement(c, ast_if.false_block, m));
		else
			n.data.if_.false_block.index = -1;

		break;
	}

	case AST_STMT_WHILE: {
		astWhile ast_while =
			astGetStatement(c->ast, ast_statement).while_;
		n.kind = HIR_WHILE;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		n.data.while_.condition = allocateNode(
			c, lowerExpression(c, ast_while.condition, m));
		n.data.while_.true_block = allocateNode(
			c, lowerStatement(c, ast_while.true_block, m));
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock ast_block =
			astGetStatement(c->ast, ast_statement).block;

		bumpMark mark = bumpCreateMark(&m->temp);
		arrayBuilder nodes_builder =
			bumpStartArrayBuilder(&m->temp, sizeof(fullNode));

		for (u16 i = 0; i < ast_block.count; i++) {
			astStatement ast_s = { .index = ast_block.start.index +
							i };

			fullNode node = lowerStatement(c, ast_s, m);
			arrayBuilderPush(&nodes_builder, &node);
		}

		fullNode *nodes =
			bumpFinishArrayBuilder(&m->temp, &nodes_builder);

		hirNode start = { .index = -1 };

		for (u16 i = 0; i < ast_block.count; i++) {
			hirNode this = allocateNode(c, nodes[i]);
			if (start.index == (u16)-1)
				start = this;
		}

		bumpClearToMark(&m->temp, mark);

		n.kind = HIR_BLOCK;
		n.type = allocateType(c, HIR_TYPE_VOID, (hirTypeData){ 0 });
		n.data.block.start = start;
		n.data.block.count = ast_block.count;
		break;
	}
	}

	assert(n.kind != (hirNodeKind)-1);
	assert(n.type.index != (u16)-1);
	return n;
}

static bool isLocalUsedInNode(hirRoot hir, hirNode node, hirLocal local)
{
	switch (hirGetNodeKind(hir, node)) {
	case HIR_MISSING:
	case HIR_INT_LITERAL:
		return false;

	case HIR_VARIABLE:
		return hirGetNode(hir, node).variable.local.index ==
		       local.index;

	case HIR_BINARY_OPERATION: {
		hirBinaryOperation binary_operation =
			hirGetNode(hir, node).binary_operation;
		return isLocalUsedInNode(hir, binary_operation.lhs, local) ||
		       isLocalUsedInNode(hir, binary_operation.rhs, local);
	}

	case HIR_ADDRESS_OF: {
		hirAddressOf address_of = hirGetNode(hir, node).address_of;
		return isLocalUsedInNode(hir, address_of.value, local);
	}

	case HIR_DEREFERENCE: {
		hirDereference dereference = hirGetNode(hir, node).dereference;
		return isLocalUsedInNode(hir, dereference.value, local);
	}

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(hir, node).assign;
		if (isLocalUsedInNode(hir, assign.rhs, local))
			return true;
		if (!isLocalUsedInNode(hir, assign.lhs, local))
			return false;

		assert(isLocalUsedInNode(hir, assign.lhs, local) &&
		       !isLocalUsedInNode(hir, assign.rhs, local));

		// Only assigning to a local and doing nothing else with it
		// does not count as using it.
		if (hirGetNodeKind(hir, assign.lhs) == HIR_VARIABLE)
			return false;

		return isLocalUsedInNode(hir, assign.lhs, local) ||
		       isLocalUsedInNode(hir, assign.rhs, local);
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(hir, node).if_;
		bool is_used = isLocalUsedInNode(hir, if_.condition, local) ||
			       isLocalUsedInNode(hir, if_.true_block, local);
		if (is_used)
			return true;
		if (if_.false_block.index != (u16)-1)
			return isLocalUsedInNode(hir, if_.false_block, local);
		return false;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(hir, node).while_;
		return isLocalUsedInNode(hir, while_.condition, local) ||
		       isLocalUsedInNode(hir, while_.true_block, local);
	}

	case HIR_RETURN: {
		hirReturn retrn = hirGetNode(hir, node).retrn;
		return isLocalUsedInNode(hir, retrn.value, local);
	}

	case HIR_BLOCK: {
		hirBlock block = hirGetNode(hir, node).block;
		for (u16 i = 0; i < block.count; i++) {
			hirNode n = { .index = block.start.index + i };
			if (isLocalUsedInNode(hir, n, local))
				return true;
		}
		return false;
	}
	}
}

hirRoot lower(astRoot ast, diagnosticsStorage *diagnostics, memory *m)
{
	bumpMark mark = bumpCreateMark(&m->temp);

	arrayBuilder functions =
		bumpStartArrayBuilder(&m->general, sizeof(hirFunction));

	ctx c = {
		.hir = {
			.functions = NULL,
			.nodes = bumpAllocateArray(hirNodeData, &m->temp, MAX_NODE_COUNT),
			.node_kinds = bumpAllocateArray(hirNodeKind, &m->temp, MAX_NODE_COUNT),
			.node_types = bumpAllocateArray(hirType, &m->temp, MAX_NODE_COUNT),
			.node_spans = bumpAllocateArray(span, &m->temp, MAX_NODE_COUNT),
			.local_names = bumpAllocateArray(identifierId, &m->temp, MAX_LOCAL_COUNT),
			.local_types = bumpAllocateArray(hirType, &m->temp, MAX_LOCAL_COUNT),
			.local_spans = bumpAllocateArray(span, &m->temp, MAX_LOCAL_COUNT),
			.types = bumpAllocateArray(hirTypeData, &m->temp, MAX_TYPE_COUNT),
			.type_kinds = bumpAllocateArray(hirTypeKind, &m->temp, MAX_TYPE_COUNT),
			.function_count = 0,
			.node_count = 0,
			.local_count = 0,
			.current_function_locals_start = { .index = -1 },
		},
		.ast = ast,
		.diagnostics = diagnostics,
		.locals_by_name = mapCreate(identifierId, hirLocal, LOCALS_BY_NAME_SLOT_COUNT, &m->temp),
		.seen_types = mapCreate(fullType, hirType, SEEN_TYPES_SLOT_COUNT, &m->temp),
	};

	for (u16 i = 0; i < c.ast.function_count; i++) {
		astFunction ast_function = c.ast.functions[i];

		if (ast_function.name.raw == (u32)-1)
			continue;

		mapClear(identifierId, hirLocal, &c.locals_by_name);

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
		arrayBuilderPush(&functions, &function);
		c.hir.function_count++;
	}

	c.hir.functions = bumpFinishArrayBuilder(&m->general, &functions);

	c.hir.nodes = bumpCopyArray(hirNodeData, &m->general, c.hir.nodes,
				    c.hir.node_count);
	c.hir.node_kinds = bumpCopyArray(hirNodeKind, &m->general,
					 c.hir.node_kinds, c.hir.node_count);
	c.hir.node_types = bumpCopyArray(hirType, &m->general, c.hir.node_types,
					 c.hir.node_count);
	c.hir.node_spans = bumpCopyArray(span, &m->general, c.hir.node_spans,
					 c.hir.node_count);

	c.hir.local_names = bumpCopyArray(identifierId, &m->general,
					  c.hir.local_names, c.hir.local_count);
	c.hir.local_types = bumpCopyArray(hirType, &m->general,
					  c.hir.local_types, c.hir.local_count);
	c.hir.local_spans = bumpCopyArray(span, &m->general, c.hir.local_spans,
					  c.hir.local_count);

	c.hir.types = bumpCopyArray(hirTypeData, &m->general, c.hir.types,
				    c.hir.type_count);
	c.hir.type_kinds = bumpCopyArray(hirTypeKind, &m->general,
					 c.hir.type_kinds, c.hir.type_count);

	bumpClearToMark(&m->temp, mark);

	for (u16 i = 0; i < c.hir.function_count; i++) {
		hirFunction function = c.hir.functions[i];
		for (u16 j = 0; j < function.locals_count; j++) {
			hirLocal local = {
				.index = function.locals_start.index + j
			};
			if (!isLocalUsedInNode(c.hir, function.body, local))
				diagnosticsStorageRecord(
					c.diagnostics, DIAG_WARNING,
					hirGetLocalSpan(c.hir, local),
					"unused variable");
		}
	}

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

span hirGetLocalSpan(hirRoot hir, hirLocal local)
{
	assert(local.index < hir.local_count);
	return hir.local_spans[local.index];
}

hirTypeData hirGetType(hirRoot hir, hirType type)
{
	assert(type.index < hir.type_count);
	return hir.types[type.index];
}

hirTypeKind hirGetTypeKind(hirRoot hir, hirType type)
{
	assert(type.index < hir.type_count);
	return hir.type_kinds[type.index];
}

u32 hirTypeSize(hirRoot hir, hirType type)
{
	switch (hirGetTypeKind(hir, type)) {
	case HIR_TYPE_VOID:
		return 0;
	case HIR_TYPE_I64:
		return 8;
	case HIR_TYPE_POINTER:
		return 8;
	}
}

void hirTypeShow(hirRoot hir, hirType type, stringBuilder *sb)
{
	switch (hirGetTypeKind(hir, type)) {
	case HIR_TYPE_VOID:
		stringBuilderPrintf(sb, "void");
		break;
	case HIR_TYPE_I64:
		stringBuilderPrintf(sb, "i64");
		break;
	case HIR_TYPE_POINTER: {
		hirPointer pointer = hirGetType(hir, type).pointer;
		stringBuilderPrintf(sb, "*");
		hirTypeShow(hir, pointer.child_type, sb);
		break;
	}
	}
}

typedef struct debugCtx {
	hirRoot hir;
	interner interner;
	stringBuilder *sb;
	u32 indentation;
	u8 pad[4];
} debugCtx;

static void newline(debugCtx *c)
{
	stringBuilderPrintf(c->sb, "\n");
	for (u32 i = 0; i < c->indentation; i++)
		stringBuilderPrintf(c->sb, "\t");
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

	case HIR_ADDRESS_OF: {
		hirAddressOf address_of = hirGetNode(c->hir, node).address_of;
		stringBuilderPrintf(c->sb, "&");
		debugNode(c, address_of.value);
		break;
	}

	case HIR_DEREFERENCE: {
		hirDereference dereference =
			hirGetNode(c->hir, node).dereference;
		stringBuilderPrintf(c->sb, "*");
		debugNode(c, dereference.value);
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
		stringBuilderPrintf(c->sb, " ");
		debugNode(c, if_.true_block);

		if (if_.false_block.index == (u16)-1)
			break;

		stringBuilderPrintf(c->sb, " else ");
		debugNode(c, if_.false_block);
		break;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(c->hir, node).while_;
		stringBuilderPrintf(c->sb, "while ");
		debugNode(c, while_.condition);
		stringBuilderPrintf(c->sb, " ");
		debugNode(c, while_.true_block);
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
		identifierId name = hirGetLocalName(c->hir, local);
		newline(c);
		stringBuilderPrintf(c->sb, "var %s ",
				    internerLookup(c->interner, name));
		hirTypeShow(c->hir, hirGetLocalType(c->hir, local), c->sb);
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

char *lowerTests(char *input, memory *m)
{
	diagnosticsStorage diagnostics = diagnosticsStorageCreate(&m->general);
	tokenBuffer buf = lex(input, &diagnostics, m);
	interner interner = intern(&buf, &input, 1, m);
	astRoot ast = parse(buf, input, &diagnostics, m);

	// Remove all diagnostics up to this point.
	diagnostics.count = 0;
	diagnostics.all_messages.bytes_used = 0;

	hirRoot hir = lower(ast, &diagnostics, m);
	stringBuilder sb = stringBuilderCreate(&m->temp);
	hirDebug(hir, interner, &sb);
	diagnosticsStorageDebug(diagnostics, &sb);
	return stringBuilderFinish(sb);
}
