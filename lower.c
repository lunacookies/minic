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

static hirNode *lowerExpression(astExpression *ast_expression,
				hirLocal **locals, memory *m)
{
	switch (ast_expression->kind) {
	case AST_EXPR_MISSING: {
		hirNode node = {
			.kind = HIR_MISSING,
			.type = HIR_TYPE_VOID,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}

	case AST_EXPR_INT_LITERAL: {
		hirNode node = {
			.kind = HIR_INT_LITERAL,
			.type = HIR_TYPE_I64,
			.value = ast_expression->value,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}

	case AST_EXPR_VARIABLE: {
		hirLocal *local = lookupLocal(ast_expression->name, *locals);
		if (local == NULL) {
			sendDiagnosticToSink(DIAG_ERROR, ast_expression->span,
					     "undefined variable");
			hirNode node = {
				.kind = HIR_MISSING,
				.type = HIR_TYPE_VOID,
			};
			hirNode *ptr =
				allocateInBump(&m->general, sizeof(hirNode));
			*ptr = node;
			return ptr;
		}

		hirNode node = {
			.kind = HIR_VARIABLE,
			.type = local->type,
			.local = local,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}
	}
}

static hirNode *lowerStatement(astStatement *ast_statement, hirLocal **locals,
			       memory *m)
{
	switch (ast_statement->kind) {
	case AST_STMT_MISSING: {
		hirNode node = {
			.kind = HIR_MISSING,
			.type = HIR_TYPE_VOID,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}

	case AST_STMT_RETURN: {
		hirNode node = {
			.kind = HIR_RETURN,
			.type = HIR_TYPE_VOID,
			.lhs = lowerExpression(ast_statement->value, locals, m),
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}

	case AST_STMT_LOCAL_DEFINITION: {
		hirLocal *existing_local =
			lookupLocal(ast_statement->name, *locals);
		if (existing_local != NULL)
			sendDiagnosticToSink(DIAG_ERROR, ast_statement->span,
					     "cannot shadow existing variable");

		hirNode *rhs = lowerExpression(ast_statement->value, locals, m);

		hirLocal *local =
			addLocal(ast_statement->name, rhs->type, locals, m);
		hirNode lhs_no_ptr = {
			.kind = HIR_VARIABLE,
			.type = local->type,
			.local = local,
		};
		hirNode *lhs = allocateInBump(&m->general, sizeof(hirNode));
		*lhs = lhs_no_ptr;

		hirNode node = {
			.kind = HIR_ASSIGN,
			.type = HIR_TYPE_VOID,
			.lhs = lhs,
			.rhs = rhs,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}

	case AST_STMT_BLOCK: {
		bumpMark mark = markBump(&m->temp);
		hirNode *children_top =
			(hirNode *)(m->temp.top + m->temp.bytes_used);
		usize count = 0;

		for (usize i = 0; i < ast_statement->count; i++) {
			astStatement *ast_child = ast_statement->statements[i];
			hirNode *child = lowerStatement(ast_child, locals, m);
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

		hirNode node = {
			.kind = HIR_BLOCK,
			.type = HIR_TYPE_VOID,
			.children = children,
			.count = count,
		};
		hirNode *ptr = allocateInBump(&m->general, sizeof(hirNode));
		*ptr = node;
		return ptr;
	}
	}
}

hirRoot lower(astRoot ast, memory *m)
{
	hirFunction *head = NULL;
	hirFunction *current_function = NULL;

	for (astFunction *ast_function = ast.functions; ast_function != NULL;
	     ast_function = ast_function->next) {
		hirLocal *locals = NULL;
		hirNode *body = lowerStatement(ast_function->body, &locals, m);

		hirFunction function = {
			.name = ast_function->name,
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

	case HIR_ASSIGN:
		printf("\033[32mset\033[0m ");
		debugNode(node->lhs, interner, indentation);
		printf(" = ");
		debugNode(node->rhs, interner, indentation);
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
