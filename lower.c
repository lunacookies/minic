#include "minic.h"

static hirLocal *lookupLocal(identifierId name, hirLocal *locals)
{
	for (hirLocal *local = locals; local != NULL; local = local->next)
		if (local->name.raw == name.raw)
			return local;
	return NULL;
}

static hirLocal *addLocal(identifierId name, hirType type, hirLocal **locals,
			  memory *m)
{
	hirLocal new_local = {
		.name = name,
		.type = type,
		.offset = 0,
		.next = *locals,
	};
	hirLocal *ptr = allocateInBump(&m->general, sizeof(hirLocal));
	*ptr = new_local;
	*locals = ptr;
	return ptr;
}

static hirNode *lowerExpression(astRoot ast, astExpression ast_expression,
				hirLocal **locals, memory *m)
{
	hirNode node = { .span = astGetExpressionSpan(ast, ast_expression) };

	switch (astGetExpressionKind(ast, ast_expression)) {
	case AST_EXPR_MISSING:
		node.kind = HIR_MISSING;
		node.type = HIR_TYPE_VOID;
		break;

	case AST_EXPR_INT_LITERAL: {
		astIntLiteral ast_int_literal =
			astGetExpression(ast, ast_expression).int_literal;
		node.kind = HIR_INT_LITERAL;
		node.type = HIR_TYPE_I64;
		node.value = ast_int_literal.value;
		break;
	}

	case AST_EXPR_VARIABLE: {
		astVariable ast_variable =
			astGetExpression(ast, ast_expression).variable;

		if (ast_variable.name.raw == (u32)-1) {
			node.kind = HIR_MISSING;
			node.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal *local = lookupLocal(ast_variable.name, *locals);
		if (local == NULL) {
			sendDiagnosticToSink(
				DIAG_ERROR,
				astGetExpressionSpan(ast, ast_expression),
				"undefined variable");
			node.kind = HIR_MISSING;
			node.type = HIR_TYPE_VOID;
			break;
		}

		node.kind = HIR_VARIABLE;
		node.type = local->type;
		node.local = local;
		break;
	}

	case AST_EXPR_BINARY_OPERATION: {
		astBinaryOperation ast_binary_operation =
			astGetExpression(ast, ast_expression).binary_operation;
		node.kind = HIR_BINARY_OPERATION;
		node.lhs = lowerExpression(ast, ast_binary_operation.lhs,
					   locals, m);
		node.rhs = lowerExpression(ast, ast_binary_operation.rhs,
					   locals, m);
		node.op = ast_binary_operation.op;
		node.type = node.lhs->type;
		break;
	}
	}

	hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
	*ptr = node;
	return ptr;
}

static hirNode *lowerStatement(astRoot ast, astStatement ast_statement,
			       hirLocal **locals, memory *m)
{
	hirNode node = { .span = astGetStatementSpan(ast, ast_statement) };

	switch (astGetStatementKind(ast, ast_statement)) {
	case AST_STMT_MISSING:
		node.kind = HIR_MISSING;
		node.type = HIR_TYPE_VOID;
		break;

	case AST_STMT_RETURN: {
		astReturn ast_retrn = astGetStatement(ast, ast_statement).retrn;
		node.kind = HIR_RETURN;
		node.type = HIR_TYPE_VOID;
		node.lhs = lowerExpression(ast, ast_retrn.value, locals, m);
		break;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		astLocalDefinition ast_local_definition =
			astGetStatement(ast, ast_statement).local_definition;

		hirLocal *existing_local =
			lookupLocal(ast_local_definition.name, *locals);
		if (existing_local != NULL)
			sendDiagnosticToSink(
				DIAG_ERROR,
				astGetStatementSpan(ast, ast_statement),
				"cannot shadow existing variable");

		hirNode *rhs = lowerExpression(ast, ast_local_definition.value,
					       locals, m);

		if (ast_local_definition.name.raw == (u32)-1) {
			node.kind = HIR_MISSING;
			node.type = HIR_TYPE_VOID;
			break;
		}

		hirLocal *local = addLocal(ast_local_definition.name, rhs->type,
					   locals, m);
		hirNode lhs_no_ptr = {
			.kind = HIR_VARIABLE,
			.type = local->type,
			.local = local,
		};
		hirNode *lhs = allocateInBump(&m->general, sizeof(hirNode));
		*lhs = lhs_no_ptr;

		node.kind = HIR_ASSIGN;
		node.type = HIR_TYPE_VOID;
		node.lhs = lhs;
		node.rhs = rhs;
		break;
	}

	case AST_STMT_ASSIGN: {
		astAssign ast_assign =
			astGetStatement(ast, ast_statement).assign;
		node.kind = HIR_ASSIGN;
		node.type = HIR_TYPE_VOID;
		node.lhs = lowerExpression(ast, ast_assign.lhs, locals, m);
		node.rhs = lowerExpression(ast, ast_assign.rhs, locals, m);
		break;
	}

	case AST_STMT_IF: {
		astIf ast_if = astGetStatement(ast, ast_statement).if_;
		node.kind = HIR_IF;
		node.type = HIR_TYPE_VOID;
		node.condition =
			lowerExpression(ast, ast_if.condition, locals, m);
		node.true_branch =
			lowerStatement(ast, ast_if.true_branch, locals, m);
		if (ast_if.false_branch.index != (u16)-1)
			node.false_branch = lowerStatement(
				ast, ast_if.false_branch, locals, m);
		break;
	}

	case AST_STMT_WHILE: {
		astWhile ast_while = astGetStatement(ast, ast_statement).while_;
		node.kind = HIR_WHILE;
		node.type = HIR_TYPE_VOID;
		node.condition =
			lowerExpression(ast, ast_while.condition, locals, m);
		node.true_branch =
			lowerStatement(ast, ast_while.true_branch, locals, m);
		break;
	}

	case AST_STMT_BLOCK: {
		astBlock ast_block = astGetStatement(ast, ast_statement).block;

		bumpMark mark = markBump(&m->temp);
		hirNode *children_top =
			(hirNode *)(m->temp.top + m->temp.bytes_used);
		usize count = 0;

		for (astStatement ast_child = ast_block.start;
		     ast_child.index < ast_block.end.index; ast_child.index++) {
			hirNode *child =
				lowerStatement(ast, ast_child, locals, m);
			if (child == NULL)
				continue;
			hirNode **ptr =
				allocateInBump(&m->temp, sizeof(hirNode *));
			*ptr = child;
			count++;
		}

		usize children_size = sizeof(hirNode *) * count;
		hirNode **children = allocateInBump(&m->general, children_size);
		memcpy(children, children_top, children_size);
		clearBumpToMark(&m->temp, mark);

		node.kind = HIR_BLOCK;
		node.type = HIR_TYPE_VOID;
		node.children = children;
		node.count = count;
		break;
	}
	}

	hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
	*ptr = node;
	return ptr;
}

hirRoot lower(astRoot ast, memory *m)
{
	hirFunction *head = NULL;
	hirFunction *current_function = NULL;

	for (u16 i = 0; i < ast.function_count; i++) {
		astFunction ast_function = ast.functions[i];

		if (ast_function.name.raw == (u32)-1)
			continue;

		hirLocal *locals = NULL;
		hirNode *body =
			lowerStatement(ast, ast_function.body, &locals, m);

		hirFunction function = {
			.name = ast_function.name,
			.body = body,
			.locals = locals,
		};
		hirFunction *ptr =
			allocateInBump(&m->general, sizeof(hirFunction));
		*ptr = function;

		if (head == NULL) {
			head = ptr;
			current_function = ptr;
		} else {
			current_function->next = ptr;
			current_function = ptr;
		}
	}

	hirRoot root = { .functions = head };
	return root;
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

static void newline(u32 indentation)
{
	printf("\n");
	for (u32 i = 0; i < indentation; i++)
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

static void debugNode(hirNode *node, interner interner, u32 indentation)
{
	switch (node->kind) {
	case HIR_MISSING:
		printf("\033[7;31m<missing>\033[0m");
		break;

	case HIR_INT_LITERAL:
		printf("\033[36m%llu\033[0m", node->value);
		break;

	case HIR_VARIABLE:
		printf("\033[35m%s\033[0m",
		       lookup(interner, node->local->name));
		break;

	case HIR_BINARY_OPERATION:
		printf("(");
		debugNode(node->lhs, interner, indentation);

		switch (node->op) {
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

		debugNode(node->rhs, interner, indentation);
		printf(")");
		break;

	case HIR_ASSIGN:
		printf("\033[32mset\033[0m ");
		debugNode(node->lhs, interner, indentation);
		printf(" = ");
		debugNode(node->rhs, interner, indentation);
		break;

	case HIR_IF:
		printf("\033[32mif\033[0m ");
		debugNode(node->condition, interner, indentation);
		indentation++;

		newline(indentation);
		debugNode(node->true_branch, interner, indentation);
		indentation--;

		if (node->false_branch != NULL) {
			newline(indentation);
			printf("\033[32melse\033[0m");
			indentation++;

			newline(indentation);
			debugNode(node->false_branch, interner, indentation);
			indentation--;
		}
		break;

	case HIR_WHILE:
		printf("\033[32mwhile\033[0m ");
		debugNode(node->condition, interner, indentation);
		indentation++;
		newline(indentation);
		debugNode(node->true_branch, interner, indentation);
		indentation--;
		break;

	case HIR_RETURN:
		printf("\033[32mreturn\033[0m ");
		debugNode(node->lhs, interner, indentation);
		break;

	case HIR_BLOCK:
		if (node->count == 0) {
			printf("{}");
			break;
		}
		printf("{");
		indentation++;
		for (usize i = 0; i < node->count; i++) {
			newline(indentation);
			debugNode(node->children[i], interner, indentation);
		}
		indentation--;
		newline(indentation);
		printf("}");
		break;
	}
}

static void debugFunction(hirFunction *function, interner interner,
			  u32 indentation)
{
	printf("\033[32mfunc \033[33m%s\033[0m",
	       lookup(interner, function->name));

	indentation++;
	for (hirLocal *local = function->locals; local != NULL;
	     local = local->next) {
		newline(indentation);
		printf("\033[32mvar \033[35m%s \033[91m%s\033[0m",
		       lookup(interner, local->name),
		       debugHirType(local->type));
	}

	newline(indentation);
	debugNode(function->body, interner, indentation);
	indentation--;
}

void debugHir(hirRoot hir, interner interner)
{
	hirFunction *f = hir.functions;
	bool first = true;
	u32 indentation = 0;

	while (f != NULL) {
		if (first)
			first = false;
		else
			newline(indentation);

		debugFunction(f, interner, indentation);
		newline(indentation);
		f = f->next;
	}
}
