#include "minic.h"

internal void GenExpression(struct expression Expression);

global_variable struct func *CurrentFunction;
global_variable usize Depth;

internal usize
LabelCount(void)
{
	local_persist usize I = 0;
	return I++;
}

internal void
Push(void)
{
	printf("\tsub\tsp, sp, #16\n");
	printf("\tstr\tx8, [sp]\n");
	Depth++;
}

internal void
Pop(char *Register)
{
	printf("\tldr\t%s, [sp]\n", Register);
	printf("\tadd\tsp, sp, #16\n");
	Depth--;
}

internal void
EmitMemcpy(char *Destination, char *Source, usize N)
{
	printf("\tmov\tx0, %s\n", Destination);
	printf("\tmov\tx1, %s\n", Source);
	printf("\tmov\tx2, #%zu\n", N);
	printf("\tbl\t_memcpy\n");
}

internal void
Load(struct type Type)
{
	switch (Type.Kind) {
	case TY_DUMMY:
		Assert(false);
	case TY_I64:
	case TY_POINTER:
		printf("\tldr\tx8, [x8]\n");
		break;
	case TY_ARRAY:
	case TY_STRUCT:
		break;
	}
}

internal void
Store(struct type Type)
{
	Pop("x9");

	switch (Type.Kind) {
	case TY_DUMMY:
		Assert(false);
	case TY_I64:
	case TY_POINTER:
		printf("\tstr\tx8, [x9]\n");
		break;
	// x8 holds the address to load from, not the value itself
	case TY_ARRAY:
	case TY_STRUCT:
		EmitMemcpy("x9", "x8", TypeSize(Type));
		break;
	}
}

internal void
GenAddress(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_VARIABLE:
		printf("\tadd\tx8, x29, #%zd\n", Expression.Local->Offset);
		break;
	case EK_DEREFERENCE:
		GenExpression(*Expression.Lhs);
		break;
	case EK_INDEX:
		GenAddress(*Expression.Array);
		Push();

		// calculate index, scaled by array element type size
		GenExpression(*Expression.Index);
		printf("\tmov\tx10, #%zu\n",
		       TypeSize(*Expression.Array->Type.ElementType));
		printf("\tmul\tx8, x8, x10\n");

		// array start address is on the top of the stack
		// and the index is in x8

		Pop("x9");
		printf("\tadd\tx8, x8, x9\n");
		break;
	case EK_FIELD_ACCESS:
		GenAddress(*Expression.Lhs);
		printf("\tadd\tx8, x8, #%zd\n", Expression.Field->Offset);
		break;
	default:
		Error("not an lvalue");
	}
}

internal void
GenCall(struct expression Expression)
{
	if (Expression.NumArguments == 0)
		goto PrintName;

	for (usize I = 0; I < Expression.NumArguments; I++) {
		GenExpression(Expression.Arguments[I]);
		Push();
	}

	// we have to do this weird contorted do-while loop
	// because I is unsigned and so “I > 0” in a for loop
	// will always return true
	usize I = Expression.NumArguments;
	do {
		I--;
		// aarch64 passes parameters in x0 to x7
		Assert(I <= 7);
		char Register[3] = "x0";
		Register[1] = '0' + I;
		Pop(Register);
	} while (I != 0);

PrintName:
	printf("\tbl\t_%s\n", Expression.Name);
}

internal void
GenBinaryExpression(struct expression Expression)
{
	struct expression *Lhs = Expression.Lhs;
	struct expression *Rhs = Expression.Rhs;
	enum binary_operator Operator = Expression.Operator;

	GenExpression(*Rhs);
	Push();
	GenExpression(*Lhs);
	Pop("x9");

	switch (Operator) {
	case OP_ADD:
	case OP_SUBTRACT:
		if (Lhs->Type.Kind == TY_POINTER && Rhs->Type.Kind == TY_I64) {
			printf("\tmov\tx10, #%zu\n",
			       TypeSize(*Lhs->Type.Pointee));
			printf("\tmul\tx9, x9, x10\n");
		}

		if (Operator == OP_ADD)
			printf("\tadd\tx8, x8, x9\n");
		else
			printf("\tsub\tx8, x8, x9\n");

		if (Lhs->Type.Kind == TY_POINTER &&
		    Rhs->Type.Kind == TY_POINTER) {
			printf("\tmov\tx9, #%zu\n",
			       TypeSize(*Lhs->Type.Pointee));
			printf("\tsdiv\tx8, x8, x9\n");
		}

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

internal void
GenExpression(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_NUMBER:
		printf("\tmov\tx8, #%zu\n", Expression.Value);
		break;

	case EK_VARIABLE:
	case EK_FIELD_ACCESS:
		GenAddress(Expression);
		Load(Expression.Type);
		break;

	case EK_CALL:
		GenCall(Expression);
		break;

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

	case EK_INDEX:
		GenAddress(Expression);
		Load(Expression.Type);
		break;
	}
}

internal void
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
		Store(Statement.Destination.Type);
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

	case SK_IF: {
		usize C = LabelCount();
		GenExpression(Statement.Expression);
		printf("\tcbz\tx8, .L.%zu.else\n", C);
		GenStatement(*Statement.TrueBranch);
		printf("\tb\t.L.%zu.end\n", C);
		printf(".L.%zu.else:\n", C);
		if (Statement.FalseBranch != NULL)
			GenStatement(*Statement.FalseBranch);
		printf(".L.%zu.end:\n", C);
		break;
	}

	case SK_WHILE: {
		usize C = LabelCount();
		printf(".L.%zu.begin:\n", C);
		GenExpression(Statement.Expression);
		printf("\tcbz\tx8, .L.%zu.end\n", C);
		GenStatement(*Statement.Body);
		printf("\tb\t.L.%zu.begin\n", C);
		printf(".L.%zu.end:\n", C);
		break;
	}
	}
}

internal void
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

internal void
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

internal usize
AlignTo(usize N, usize Align)
{
	return (N + Align - 1) / Align * Align;
}

internal void
StackAllocate(void)
{
	isize Offset = 0;
	for (usize I = 0; I < CurrentFunction->NumLocals; I++) {
		struct local *Local = &CurrentFunction->Locals[I];
		Offset += TypeSize(Local->Type);
		Local->Offset = -Offset;
	}

	// aarch64 requires the stack pointer
	// to always have an alignment of 16
	CurrentFunction->StackSize = AlignTo(Offset, 16);
}

internal void
GenFunction(struct func *Function)
{
	CurrentFunction = Function;
	StackAllocate();

	printf(".global _%s\n", Function->Name);
	printf(".align 2\n");
	printf("_%s:\n", Function->Name);
	GenPrologue();

	// save passed-by-register arguments to the stack
	// right next to other local variables
	// so they can be treated as local variables
	for (usize I = 0; I < Function->NumParameters; I++) {
		// aarch64 passes parameters in x0 to x7
		Assert(I <= 7);

		// parameters are guaranteed to be pushed to the Locals vector
		// before actual local variables
		printf("\tstr\tx%zu, [x29, #%zd]\n", I,
		       Function->Locals[I].Offset);
	}

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
