#include "minic.h"

typedef struct ctx {
	hirRoot hir;
	u32 id;
	char *function_name;
	stringBuilder *assembly;
	diagnosticsStorage *diagnostics;
	u32 *local_offsets;
	u32 *temporary_offsets;
} ctx;

static u32 roundUpTo(u32 x, u32 multiple_of)
{
	return ((x + multiple_of - 1) / multiple_of) * multiple_of;
}

static void allocateLocals(ctx *c, u32 *offset, hirFunction function)
{
	for (u16 i = 0; i < function.locals_count; i++) {
		hirLocal local = hirLocalMake(function.locals_start.index + i);
		hirType type = hirGetLocalType(c->hir, local);
		u32 size = hirTypeSize(c->hir, type);

		*offset = roundUpTo(*offset, size); // align

		// We step forward by the size of the type
		// *before* storing this local’s offset
		// because the offset is actually negative
		// (from the stack top).
		*offset += size;

		c->local_offsets[local.index] = *offset;
	}
}

static void allocateTemporaries(ctx *c, u32 *offset, hirNode node)
{
	c->temporary_offsets[node.index] = -1;

	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_MISSING:
	case HIR_INT_LITERAL:
	case HIR_VARIABLE:
		break;

	case HIR_BINARY_OPERATION: {
		hirBinaryOperation binary_operation =
			hirGetNode(c->hir, node).binary_operation;
		allocateTemporaries(c, offset, binary_operation.lhs);
		allocateTemporaries(c, offset, binary_operation.rhs);
		break;
	}

	case HIR_ADDRESS_OF: {
		hirAddressOf address_of = hirGetNode(c->hir, node).address_of;
		allocateTemporaries(c, offset, address_of.value);
		break;
	}

	case HIR_DEREFERENCE: {
		hirDereference dereference =
			hirGetNode(c->hir, node).dereference;
		allocateTemporaries(c, offset, dereference.value);
		break;
	}

	case HIR_INDEX: {
		hirIndex index = hirGetNode(c->hir, node).index;
		allocateTemporaries(c, offset, index.index);
		allocateTemporaries(c, offset, index.array);
		break;
	}

	case HIR_ARRAY_LITERAL: {
		hirArrayLiteral array_literal =
			hirGetNode(c->hir, node).array_literal;

		for (u16 i = 0; i < array_literal.count; i++) {
			hirNode n = hirNodeMake(array_literal.start.index + i);
			allocateTemporaries(c, offset, n);
		}

		hirType child_type = hirGetNodeType(c->hir, node);
		u32 size =
			hirTypeSize(c->hir, child_type) * array_literal.count;

		*offset = roundUpTo(*offset, size); // align

		// We step forward by the size of the type
		// *before* storing this local’s offset
		// because the offset is actually negative
		// (from the stack top).
		*offset += size;

		c->temporary_offsets[node.index] = *offset;

		break;
	}

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(c->hir, node).assign;
		allocateTemporaries(c, offset, assign.lhs);
		allocateTemporaries(c, offset, assign.rhs);
		break;
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(c->hir, node).if_;
		allocateTemporaries(c, offset, if_.condition);
		allocateTemporaries(c, offset, if_.true_block);
		if (if_.false_block.index != (u16)-1)
			allocateTemporaries(c, offset, if_.false_block);
		break;
	}

	case HIR_WHILE: {
		hirWhile while_ = hirGetNode(c->hir, node).while_;
		allocateTemporaries(c, offset, while_.condition);
		allocateTemporaries(c, offset, while_.true_block);
		break;
	}

	case HIR_RETURN: {
		hirReturn retrn = hirGetNode(c->hir, node).retrn;
		allocateTemporaries(c, offset, retrn.value);
		break;
	}

	case HIR_BLOCK: {
		hirBlock block = hirGetNode(c->hir, node).block;
		for (u16 i = 0; i < block.count; i++) {
			hirNode n = hirNodeMake(block.start.index + i);
			allocateTemporaries(c, offset, n);
		}
		break;
	}
	}
}

static u32 calculateStackLayout(ctx *c, hirFunction function)
{
	u32 offset = 0;

	allocateLocals(c, &offset, function);
	allocateTemporaries(c, &offset, function.body);

	// in AArch64 sp must always be aligned to 16
	return roundUpTo(offset, 16);
}

static void directive(ctx *c, const char *directive_name, const char *fmt, ...)
{
	stringBuilderPrintf(c->assembly, ".%s ", directive_name);
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(c->assembly, fmt, ap);
	va_end(ap);
	stringBuilderPrintf(c->assembly, "\n");
}

static void label(ctx *c, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	stringBuilderPrintfV(c->assembly, fmt, ap);
	va_end(ap);
	stringBuilderPrintf(c->assembly, ":\n");
}

static void instruction(ctx *c, const char *instruction_mnemonic,
			const char *fmt, ...)
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

static void pop(ctx *c, const char *reg)
{
	instruction(c, "ldr", "%s, [sp]", reg);
	instruction(c, "add", "sp, sp, #16");
}

static void emitMemcpy(ctx *c, const char *dst, const char *src, u32 num_bytes)
{
	instruction(c, "mov", "x0, %s", dst);
	instruction(c, "mov", "x1, %s", src);
	instruction(c, "mov", "x2, #%u", num_bytes);
	instruction(c, "bl", "_memcpy");
}

static void load(ctx *c, hirType type)
{
	switch (hirGetTypeKind(c->hir, type)) {
	case HIR_TYPE_VOID:
		break;

	case HIR_TYPE_I64:
	case HIR_TYPE_POINTER:
		instruction(c, "ldr", "x8, [x8]");
		break;

	case HIR_TYPE_ARRAY:
		break;
	}
}

static void store(ctx *c, hirType type)
{
	pop(c, "x9");

	switch (hirGetTypeKind(c->hir, type)) {
	case HIR_TYPE_VOID:
		break;

	case HIR_TYPE_I64:
	case HIR_TYPE_POINTER:
		instruction(c, "str", "x8, [x9]");
		break;

	case HIR_TYPE_ARRAY:
		emitMemcpy(c, "x9", "x8", hirTypeSize(c->hir, type));
		break;
	}
}

static void gen(ctx *c, hirNode node);

static void genAddress(ctx *c, hirNode node)
{
	switch (hirGetNodeKind(c->hir, node)) {
	case HIR_VARIABLE: {
		hirVariable variable = hirGetNode(c->hir, node).variable;
		u32 offset = c->local_offsets[variable.local.index];
		instruction(c, "sub", "x8, fp, #%u", offset);
		break;
	}

	case HIR_DEREFERENCE: {
		hirDereference dereference =
			hirGetNode(c->hir, node).dereference;
		gen(c, dereference.value);
		break;
	}

	case HIR_INDEX: {
		hirIndex index = hirGetNode(c->hir, node).index;
		u32 size = hirTypeSize(c->hir, hirGetNodeType(c->hir, node));
		genAddress(c, index.array);
		push(c);
		gen(c, index.index);
		instruction(c, "mov", "x9, #%d", size);
		instruction(c, "mul", "x8, x8, x9");
		pop(c, "x9");
		instruction(c, "add", "x8, x8, x9");
		break;
	}

	case HIR_ARRAY_LITERAL: {
		hirArrayLiteral array_literal =
			hirGetNode(c->hir, node).array_literal;
		hirType type = hirGetNodeType(c->hir, node);
		assert(hirGetTypeKind(c->hir, type) == HIR_TYPE_ARRAY);
		hirType child_type = hirGetType(c->hir, type).array.child_type;

		u32 offset = c->temporary_offsets[node.index];

		for (u16 i = 0; i < array_literal.count; i++) {
			hirNode n = hirNodeMake(array_literal.start.index + i);
			assert(hirGetNodeType(c->hir, n).index ==
			       child_type.index);

			u32 element_offset =
				offset - hirTypeSize(c->hir, child_type) * i;
			instruction(c, "sub", "x8, fp, #%u", element_offset);
			push(c);

			gen(c, n);

			store(c, child_type);
		}

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

	case HIR_VARIABLE:
		genAddress(c, node);
		load(c, hirGetNodeType(c->hir, node));
		break;

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

	case HIR_ADDRESS_OF: {
		hirAddressOf address_of = hirGetNode(c->hir, node).address_of;
		genAddress(c, address_of.value);
		break;
	}

	case HIR_DEREFERENCE: {
		hirDereference dereference =
			hirGetNode(c->hir, node).dereference;
		gen(c, dereference.value);
		load(c, hirGetNodeType(c->hir, node));
		break;
	}

	case HIR_INDEX:
		genAddress(c, node);
		load(c, hirGetNodeType(c->hir, node));
		break;

	case HIR_ARRAY_LITERAL:
		genAddress(c, node);
		break;

	case HIR_ASSIGN: {
		hirAssign assign = hirGetNode(c->hir, node).assign;
		hirType type = hirGetNodeType(c->hir, assign.rhs);
		genAddress(c, assign.lhs);
		push(c);
		gen(c, assign.rhs);
		store(c, type);
		break;
	}

	case HIR_IF: {
		hirIf if_ = hirGetNode(c->hir, node).if_;
		u32 i = c->id;
		c->id++;
		gen(c, if_.condition);
		instruction(c, "cbz", "x8, ELSE_%s_%u", c->function_name, i);
		gen(c, if_.true_block);
		instruction(c, "b", "ENDIF_%s_%u", c->function_name, i);
		label(c, "ELSE_%s_%u", c->function_name, i);
		if (if_.false_block.index != (u16)-1)
			gen(c, if_.false_block);
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
		gen(c, while_.true_block);
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
			hirNode n = hirNodeMake(block.start.index + i);
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
	bumpMark mark = bumpCreateMark(&m->temp);

	ctx c = {
		.hir = hir,
		.id = 0,
		.function_name = NULL,
		.assembly = assembly,
		.diagnostics = diagnostics,
		.local_offsets =
			bumpAllocateArray(u32, &m->temp, hir.local_count),
		.temporary_offsets =
			bumpAllocateArray(u32, &m->temp, hir.node_count),
	};

	for (u16 i = 0; i < hir.function_count; i++) {
		hirFunction function = hir.functions[i];

		c.function_name = internerLookup(interner, function.name);
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

	bumpClearToMark(&m->temp, mark);
}
