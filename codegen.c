#include "minic.h"

void GenExpression(struct expression Expression);

global_variable struct func *CurrentFunction;
global_variable usize Depth;

static void
Push(void)
{
	printf("\tsub\tsp, sp, #16\n");
	printf("\tstr\tx8, [sp]\n");
	Depth++;
}

static void
Pop(char *arg)
{
	printf("\tldr\t%s, [sp]\n", arg);
	printf("\tadd\tsp, sp, #16\n");
	Depth--;
}

static void
Store(void)
{
	Pop("x9");
	printf("\tstr\tx8, [x9]\n");
}

void
GenAddress(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_VARIABLE:
		printf("\tadd\tx8, x29, #%zd\n", Expression.Local->Offset);
		break;
	case EK_DEREFERENCE:
		GenExpression(*Expression.Lhs);
		break;
	default:
		Error("not an lvalue");
	}
}

void
GenBinaryExpression(struct expression Expression)
{
	GenExpression(*Expression.Rhs);
	Push();
	GenExpression(*Expression.Lhs);
	Pop("x9");

	switch (Expression.Operator) {
	case OP_ADD:
		printf("\tadd\tx8, x8, x9\n");
		break;

	case OP_SUBTRACT:
		printf("\tsub\tx8, x8, x9\n");
		break;

	case OP_MULTIPLY:
		printf("\tmul\tx8, x8, x9\n");
		break;

	case OP_DIVIDE:
		printf("\tsdiv\tx8, x8, x9\n");
		break;

	case OP_AND:
		printf("\tand\tx8, x8, x9\n");
		break;

	case OP_OR:
		printf("\torr\tx8, x8, x9\n");
		break;

	case OP_EQUAL:
		printf("\tcmp\tx8, x9\n");
		printf("\tcset\tx8, eq\n");
		break;

	case OP_NOT_EQUAL:
		printf("\tcmp\tx8, x9\n");
		printf("\tcset\tx8, ne\n");
		break;

	case OP_LESS_THAN:
		printf("\tcmp\tx8, x9\n");
		printf("\tcset\tx8, lt\n");
		break;

	case OP_LESS_THAN_EQUAL:
		printf("\tcmp\tx8, x9\n");
		printf("\tcset\tx8, le\n");
		break;
	}
}

void
GenExpression(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_NUMBER:
		printf("\tmov\tx8, #%zu\n", Expression.Value);
		break;

	case EK_VARIABLE:
		printf("\tldr\tx8, [x29, #%zd]\n", Expression.Local->Offset);
		break;

	case EK_CALL:
		Assert(false);

	case EK_BINARY:
		GenBinaryExpression(Expression);
		break;

	case EK_NOT:
		Assert(false);
		break;

	case EK_ADDRESS_OF:
		GenAddress(*Expression.Lhs);
		break;

	case EK_DEREFERENCE:
		GenExpression(*Expression.Lhs);
		printf("\tldr\tx8, [x8]\n");
		break;
	}
}

void
GenStatement(struct statement Statement)
{
	switch (Statement.Kind) {
	case SK_VAR:
		// ST_VAR actually serves no purpose at the moment
		break;

	case SK_SET:
		GenAddress(Statement.Destination);
		Push();
		GenExpression(Statement.Source);
		Store();
		break;

	case SK_BLOCK:
		for (usize I = 0; I < Statement.NumStatements; I++)
			GenStatement(Statement.Statements[I]);
		break;

	case SK_EXPRESSION:
		GenExpression(Statement.Expression);
		break;

	case SK_RETURN:
		GenExpression(Statement.Expression);
		printf("\tmov\tx0, x8\n");
		printf("\tb\t.L.return.%s\n", CurrentFunction->Name);
		break;
	}
}

void
GenPrologue()
{
	// allocate 16 bytes on the stack for the frame record
	printf("\tsub\tsp, sp, #16\n");
	printf("\tstp\tx29, x30, [sp]\n");

	// the frame pointer always points to the frame record
	printf("\tmov\tx29, sp\n");

	// allocate enough space for all local variables
	printf("\tsub\tsp, sp, #%zu\n", CurrentFunction->StackSize);
}

void
GenEpilogue()
{
	// deallocate locals
	printf("\tadd\tsp, sp, #%zu\n", CurrentFunction->StackSize);

	// now sp points at the frame record

	// restore link register and caller’s frame pointer
	printf("\tldp\tx29, x30, [sp]\n");

	// deallocate frame record
	printf("\tadd\tsp, sp, #16\n");
}

usize
AlignTo(usize N, usize Align)
{
	return (N + Align - 1) / Align * Align;
}

void
StackAllocate(void)
{
	isize Offset = 0;
	for (usize I = 0; I < CurrentFunction->NumLocals; I++) {
		struct local *Local = &CurrentFunction->Locals[I];
		Offset += Local->Size;
		Local->Offset = -Offset;
	}

	// aarch64 requires the stack pointer
	// to always have an alignment of 16
	CurrentFunction->StackSize = AlignTo(Offset, 16);
}

void
GenFunction(struct func *Function)
{
	CurrentFunction = Function;
	StackAllocate();

	printf(".global _%s\n", Function->Name);
	printf(".align 2\n");
	printf("_%s:\n", Function->Name);
	GenPrologue();

	GenStatement(Function->Body);
	Assert(Depth == 0); // all Push and Pop calls must be matched

	printf(".L.return.%s:\n", Function->Name);
	GenEpilogue();
	printf("\tret\n");
}

void
Codegen(struct ast Ast)
{
	for (usize I = 0; I < Ast.NumFunctions; I++)
		GenFunction(&Ast.Functions[I]);
}
