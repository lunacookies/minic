#include "minic.h"

static u32 roundUpTo(u32 x, u32 multiple_of)
{
	return ((x + multiple_of - 1) / multiple_of) * multiple_of;
}

static void calculateStackLayout(hirFunction *function)
{
	u32 offset = 0;
	for (hirLocal *local = function->locals; local != NULL;
	     local = local->next) {
		u32 size = typeSize(local->type);
		offset = roundUpTo(offset, size); // align
		local->offset = offset;
		offset += size;
	}

	// in AArch64 sp must always be aligned to 16
	function->stack_size = roundUpTo(offset, 16);
}

static void directive(char **s, char *directive_name, char *fmt, ...)
{
	*s += snprintf(*s, 1024, ".%s ", directive_name);
	va_list ap;
	va_start(ap, fmt);
	*s += vsnprintf(*s, 1024, fmt, ap);
	va_end(ap);
	*s += snprintf(*s, 2, "\n");
}

static void label(char **s, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	*s += vsnprintf(*s, 1024, fmt, ap);
	va_end(ap);
	*s += snprintf(*s, 3, ":\n");
}

static void instruction(char **s, char *instruction_mnemonic, char *fmt, ...)
{
	*s += snprintf(*s, 1024, "\t%s\t", instruction_mnemonic);
	va_list ap;
	va_start(ap, fmt);
	*s += vsnprintf(*s, 1024, fmt, ap);
	va_end(ap);
	*s += snprintf(*s, 2, "\n");
}

static void push(char **s)
{
	instruction(s, "sub", "sp, sp, #16");
	instruction(s, "str", "x8, [sp]");
}

static void pop(char **s, char *reg)
{
	instruction(s, "ldr", "%s, [sp]", reg);
	instruction(s, "add", "sp, sp, #16");
}

static void genAddress(char **s, hirNode *node)
{
	switch (node->kind) {
	case HIR_VARIABLE:
		instruction(s, "sub", "x8, fp, #%u", node->local->offset);
		break;

	default:
		sendDiagnosticToSink(DIAG_ERROR, node->span, "not an lvalue");
		break;
	}
}

static void gen(char **s, hirNode *node, char *function_name, u32 *id)
{
	switch (node->kind) {
	case HIR_MISSING:
		break;

	case HIR_INT_LITERAL:
		instruction(s, "mov", "x8, #%d", node->value);
		break;

	case HIR_VARIABLE:
		instruction(s, "ldr", "x8, [fp, #-%u]", node->local->offset);
		break;

	case HIR_BINARY_OPERATION:
		gen(s, node->lhs, function_name, id);
		push(s);
		gen(s, node->rhs, function_name, id);
		instruction(s, "mov", "x9, x8");
		pop(s, "x8");
		switch (node->op) {
		case AST_BINOP_ADD:
			instruction(s, "add", "x8, x8, x9");
			break;
		case AST_BINOP_SUBTRACT:
			instruction(s, "sub", "x8, x8, x9");
			break;
		case AST_BINOP_MULTIPLY:
			instruction(s, "mul", "x8, x8, x9");
			break;
		case AST_BINOP_DIVIDE:
			instruction(s, "sdiv", "x8, x8, x9");
			break;
		case AST_BINOP_EQUAL:
			instruction(s, "cmp", "x8, x9");
			instruction(s, "cset", "x8, eq");
			break;
		case AST_BINOP_NOT_EQUAL:
			instruction(s, "cmp", "x8, x9");
			instruction(s, "cset", "x8, ne");
			break;
		}
		break;

	case HIR_ASSIGN:
		genAddress(s, node->lhs);
		push(s);
		gen(s, node->rhs, function_name, id);
		pop(s, "x9");
		instruction(s, "str", "x8, [x9]");
		break;

	case HIR_IF: {
		u32 i = *id;
		(*id)++;
		gen(s, node->condition, function_name, id);
		instruction(s, "cbz", "x8, ELSE_%s_%u", function_name, i);
		gen(s, node->true_branch, function_name, id);
		instruction(s, "b", "ENDIF_%s_%u", function_name, i);
		label(s, "ELSE_%s_%u", function_name, i);
		gen(s, node->false_branch, function_name, id);
		label(s, "ENDIF_%s_%u", function_name, i);
		break;
	}

	case HIR_RETURN:
		gen(s, node->lhs, function_name, id);
		instruction(s, "mov", "x0, x8");
		instruction(s, "b", "RETURN_%s", function_name);
		break;

	case HIR_BLOCK:
		for (usize i = 0; i < node->count; i++)
			gen(s, node->children[i], function_name, id);
		break;
	}
}

static void genPrologue(char **s, hirFunction *function)
{
	// allocate 16 bytes on the stack for the frame record
	instruction(s, "sub", "sp, sp, #16");
	instruction(s, "stp", "fp, lr, [sp]");

	// now sp points at the frame record

	// the frame pointer always points to the frame record
	instruction(s, "mov", "fp, sp");

	// allocate enough space for all local variables
	instruction(s, "sub", "sp, sp, #%u", function->stack_size);
}

static void genEpilogue(char **s, hirFunction *function)
{
	// deallocate locals
	instruction(s, "add", "sp, sp, #%u", function->stack_size);

	// now sp points at the frame record

	// restore link register and caller’s frame pointer
	instruction(s, "ldp", "fp, lr, [sp]");

	// deallocate frame record
	instruction(s, "add", "sp, sp, #16");
}

void codegen(hirRoot hir, interner interner, memory *m)
{
	char *assembly_top = (char *)(m->assembly.top + m->assembly.bytes_used);
	char **s = &assembly_top;

	for (hirFunction *function = hir.functions; function != NULL;
	     function = function->next) {
		calculateStackLayout(function);

		char *function_name = (char *)lookup(interner, function->name);

		directive(s, "global", "_%s", function_name);
		directive(s, "align", "2");
		label(s, "_%s", function_name);

		genPrologue(s, function);

		u32 id = 0;
		gen(s, function->body, function_name, &id);

		label(s, "RETURN_%s", function_name);
		genEpilogue(s, function);
		instruction(s, "ret", "");

		snprintf(*s, 2, "\n");
		(*s)++;
	}

	m->assembly.bytes_used = assembly_top - (char *)m->assembly.top;
}
