#include "minic.h"

global_variable struct func *CurrentFunction;

void
GenAddress(struct expression Expression)
{
	abort();
}

void
GenExpression(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_NUMBER:
		printf("\tmov\tx8, #%zu\n", Expression.Value);
		return;
	case EK_VARIABLE:
		printf("\tldr\tx8, [x29, #%zu]\n", Expression.Local->Offset);
		return;
	case EK_CALL:
		abort();
	case EK_BINARY:
		abort();
	case EK_NOT:
		abort();
	case EK_ADDRESS_OF:
		GenAddress(Expression);
		return;
	case EK_DEREFERENCE:
		GenAddress(Expression);
		printf("\tldr\tx8, [x8]\n");
	}
}

void
GenStatement(struct statement Statement)
{
	switch (Statement.Kind) {
	case SK_VAR:
		// ST_VAR actually serves no purpose at the moment
		return;
	case SK_BLOCK:
		for (usize I = 0; I < Statement.NumStatements; I++)
			GenStatement(Statement.Statements[I]);
		return;
	case SK_EXPRESSION:
		GenExpression(Statement.Expression);
		return;
	case SK_RETURN:
		GenExpression(Statement.Expression);
		printf("\tmov\tx0, x8\n");
		printf("\tb\t.L.return.%s\n", CurrentFunction->Name);
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
	usize Offset = 0;
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
