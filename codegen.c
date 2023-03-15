#include "minic.h"

typedef struct ctx {
	hirRoot hir;
	u32 id;
	char *function_name;
	stringBuilder *assembly;
	diagnosticsStorage *diagnostics;
	u32 *local_offsets;
} ctx;

static u32 roundUpTo(u32 x, u32 multiple_of)
{
	return ((x + multiple_of - 1) / multiple_of) * multiple_of;
}

static u32 calculateStackLayout(ctx *c, hirFunction function)
{
	u32 offset = 0;
	for (u16 i = 0; i < function.locals_count; i++) {
		hirLocal local = { .index = function.locals_start.index + i };
		u32 size = hirTypeSize(hirGetLocalType(c->hir, local));
		offset = roundUpTo(offset, size); // align
		c->local_offsets[local.index] = offset;
		offset += size;
	}

	// in AArch64 sp must always be aligned to 16
	return roundUpTo(offset, 16);
}

static void directive(ctx *c, char *directive_name, char *fmt, ...)
{
	stringBuilderPrintf(c->assembly, ".%s ", directive_name);
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(c->assembly, fmt, ap);
	va_end(ap);
	stringBuilderPrintf(c->assembly, "\n");
}

static void label(ctx *c, char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(c->assembly, fmt, ap);
	va_end(ap);
	stringBuilderPrintf(c->assembly, ":\n");
}

static void instruction(ctx *c, char *instruction_mnemonic, char *fmt, ...)
{
	stringBuilderPrintf(c->assembly, "\t%s\t", instruction_mnemonic);
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(c->assembly, fmt, ap);
	va_end(ap);
	stringBuilderPrintf(c->assembly, "\n");
}

static void push(ctx *c)
{
	instruction(c, "sub", "sp, sp, #16");
	instruction(c, "str", "x8, [sp]");
}

static void pop(ctx *c, char *reg)
{
	instruction(c, "ldr", "%s, [sp]", reg);
	instruction(c, "add", "sp, sp, #16");
}

static void genAddress(ctx *c, hirNode node)
{
	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_VARIABLE: {
		hirVariable variable = hirGetNode(c->hir, node).variable;
		u32 offset = c->local_offsets[variable.local.index];
		instruction(c, "sub", "x8, fp, #%u", offset);
		break;
	}

	default:
		diagnosticsStorageRecord(c->diagnostics, DIAG_ERROR,
					 hirGetNodeSpan(c->hir, node),
					 "not an lvalue");
		break;
	}
}

static void gen(ctx *c, hirNode node)
{
	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_MISSING:
		break;

	case HIR_INT_LITERAL: {
		hirIntLiteral int_literal =
			hirGetNode(c->hir, node).int_literal;
		instruction(c, "mov", "x8, #%d", int_literal.value);
		break;
	}

	case HIR_VARIABLE: {
		hirVariable variable = hirGetNode(c->hir, node).variable;
		u32 offset = c->local_offsets[variable.local.index];
		instruction(c, "ldr", "x8, [fp, #-%u]", offset);
		break;
	}

	case HIR_BINARY_OPERATION: {
		hirBinaryOperation binary_operation =
			hirGetNode(c->hir, node).binary_operation;
		gen(c, binary_operation.lhs);
		push(c);
		gen(c, binary_operation.rhs);
		instruction(c, "mov", "x9, x8");
		pop(c, "x8");
		switch (binary_operation.op) {
		case AST_BINOP_ADD:
			instruction(c, "add", "x8, x8, x9");
			break;
		case AST_BINOP_SUBTRACT:
			instruction(c, "sub", "x8, x8, x9");
			break;
		case AST_BINOP_MULTIPLY:
			instruction(c, "mul", "x8, x8, x9");
			break;
		case AST_BINOP_DIVIDE:
			instruction(c, "sdiv", "x8, x8, x9");
			break;
		case AST_BINOP_EQUAL:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, eq");
			break;
		case AST_BINOP_NOT_EQUAL:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, ne");
			break;
		case AST_BINOP_LESS_THAN:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, lt");
			break;
		case AST_BINOP_LESS_THAN_EQUAL:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, le");
			break;
		case AST_BINOP_GREATER_THAN:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, gt");
			break;
		case AST_BINOP_GREATER_THAN_EQUAL:
			instruction(c, "cmp", "x8, x9");
			instruction(c, "cset", "x8, ge");
			break;
		}
		break;
	}

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(c->hir, node).assign;
		genAddress(c, assign.lhs);
		push(c);
		gen(c, assign.rhs);
		pop(c, "x9");
		instruction(c, "str", "x8, [x9]");
		break;
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(c->hir, node).if_;
		u32 i = c->id;
		c->id++;
		gen(c, if_.condition);
		instruction(c, "cbz", "x8, ELSE_%s_%u", c->function_name, i);
		gen(c, if_.true_branch);
		instruction(c, "b", "ENDIF_%s_%u", c->function_name, i);
		label(c, "ELSE_%s_%u", c->function_name, i);
		if (if_.false_branch.index != (u16)-1)
			gen(c, if_.false_branch);
		label(c, "ENDIF_%s_%u", c->function_name, i);
		break;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(c->hir, node).while_;
		u32 i = c->id;
		c->id++;
		label(c, "WHILE_%s_%u", c->function_name, i);
		gen(c, while_.condition);
		instruction(c, "cbz", "x8, ENDWHILE_%s_%u", c->function_name,
			    i);
		gen(c, while_.true_branch);
		instruction(c, "b", "WHILE_%s_%u", c->function_name, i);
		label(c, "ENDWHILE_%s_%u", c->function_name, i);
		break;
	}

	case HIR_RETURN: {
		hirReturn retrn = hirGetNode(c->hir, node).retrn;
		gen(c, retrn.value);
		instruction(c, "mov", "x0, x8");
		instruction(c, "b", "RETURN_%s", c->function_name);
		break;
	}

	case HIR_BLOCK: {
		hirBlock block = hirGetNode(c->hir, node).block;
		for (u16 i = 0; i < block.count; i++) {
			hirNode n = { .index = block.start.index + i };
			gen(c, n);
		}
		break;
	}
	}
}

static void genPrologue(ctx *c, u32 stack_size)
{
	// allocate 16 bytes on the stack for the frame record
	instruction(c, "sub", "sp, sp, #16");
	instruction(c, "stp", "fp, lr, [sp]");

	// now sp points at the frame record

	// the frame pointer always points to the frame record
	instruction(c, "mov", "fp, sp");

	// allocate enough space for all local variables
	instruction(c, "sub", "sp, sp, #%u", stack_size);
}

static void genEpilogue(ctx *c, u32 stack_size)
{
	// deallocate locals
	instruction(c, "add", "sp, sp, #%u", stack_size);

	// now sp points at the frame record

	// restore link register and caller’s frame pointer
	instruction(c, "ldp", "fp, lr, [sp]");

	// deallocate frame record
	instruction(c, "add", "sp, sp, #16");
}

void codegen(hirRoot hir, interner interner, stringBuilder *assembly,
	     diagnosticsStorage *diagnostics, memory *m)
{
	ctx c = {
		.hir = hir,
		.id = 0,
		.function_name = NULL,
		.assembly = assembly,
		.diagnostics = diagnostics,
		.local_offsets =
			bumpAllocateArray(u32, &m->general, hir.local_count),
	};

	for (u16 i = 0; i < hir.function_count; i++) {
		hirFunction function = hir.functions[i];

		c.function_name =
			(char *)internerLookup(interner, function.name);
		c.id = 0;

		u32 stack_size = calculateStackLayout(&c, function);

		directive(&c, "global", "_%s", c.function_name);
		directive(&c, "align", "2");
		label(&c, "_%s", c.function_name);

		genPrologue(&c, stack_size);

		gen(&c, function.body);

		label(&c, "RETURN_%s", c.function_name);
		genEpilogue(&c, stack_size);
		instruction(&c, "ret", "");

		stringBuilderPrintf(c.assembly, "\n");
	}
}
