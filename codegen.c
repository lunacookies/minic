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

static void directive(char *directive_name, char *fmt, ...)
{
	printf(".%s ", directive_name);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void label(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf(":\n");
}

static void instruction(char *instruction_mnemonic, char *fmt, ...)
{
	printf("\t%s\t", instruction_mnemonic);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

static void push(void)
{
	instruction("sub", "sp, sp, #16");
	instruction("str", "x8, [sp]");
}

static void pop(char *reg)
{
	instruction("ldr", "%s, [sp]", reg);
	instruction("add", "sp, sp, #16");
}

static void genAddress(hirNode *node)
{
	switch (node->kind) {
	case HIR_VARIABLE:
		instruction("sub", "x8, fp, #%u", node->local->offset);
		break;

	default:
		sendDiagnosticToSink(DIAG_ERROR, node->span, "not an lvalue");
		break;
	}
}

static void gen(hirNode *node, identifierId function_name, interner interner)
{
	switch (node->kind) {
	case HIR_MISSING:
		break;

	case HIR_INT_LITERAL:
		instruction("mov", "x8, #%d", node->value);
		break;

	case HIR_VARIABLE:
		instruction("ldr", "x8, [fp, #-%u]", node->local->offset);
		break;

	case HIR_ASSIGN:
		genAddress(node->lhs);
		push();
		gen(node->rhs, function_name, interner);
		pop("x9");
		instruction("str", "x8, [x9]");
		break;

	case HIR_RETURN:
		gen(node->lhs, function_name, interner);
		instruction("mov", "x0, x8");
		instruction("b", "RETURN_%s", lookup(interner, function_name));
		break;

	case HIR_BLOCK:
		for (usize i = 0; i < node->count; i++)
			gen(node->children[i], function_name, interner);
		break;
	}
}

static void genPrologue(hirFunction *function)
{
	// allocate 16 bytes on the stack for the frame record
	instruction("sub", "sp, sp, #16");
	instruction("stp", "fp, lr, [sp]");

	// now sp points at the frame record

	// the frame pointer always points to the frame record
	instruction("mov", "fp, sp");

	// allocate enough space for all local variables
	instruction("sub", "sp, sp, #%u", function->stack_size);
}

static void genEpilogue(hirFunction *function)
{
	// deallocate locals
	instruction("add", "sp, sp, #%u", function->stack_size);

	// now sp points at the frame record

	// restore link register and caller’s frame pointer
	instruction("ldp", "fp, lr, [sp]");

	// deallocate frame record
	instruction("add", "sp, sp, #16");
}

void codegen(hirRoot hir, interner interner)
{
	bool is_first = true;
	for (hirFunction *function = hir.functions; function != NULL;
	     function = function->next) {
		calculateStackLayout(function);

		if (is_first)
			is_first = false;
		else
			printf("\n");

		directive("global", "_%s", lookup(interner, function->name));
		directive("align", "2");
		label("_%s", lookup(interner, function->name));

		genPrologue(function);
		gen(function->body, function->name, interner);

		label("RETURN_%s", lookup(interner, function->name));
		genEpilogue(function);
		instruction("ret", "");
	}
}
