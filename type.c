#include "minic.h"

struct type
CreateDummyType(void)
{
	struct type Type = {
		.Kind = TY_DUMMY,
	};
	return Type;
}

struct type
CreateI64Type(void)
{
	struct type Type = {
		.Kind = TY_I64,
	};
	return Type;
}

struct type
CreateArrayType(struct type ElementType, usize NumElements)
{
	struct type Type = {
		.Kind = TY_ARRAY,
		.ElementType = malloc(sizeof(struct type)),
		.NumElements = NumElements,
	};
	*Type.ElementType = ElementType;
	return Type;
}

void
DebugType(struct type Type)
{
	switch (Type.Kind) {
	case TY_DUMMY:
		Assert(false);
	case TY_I64:
		fprintf(stderr, "\033[95mi64\033[0m");
		break;
	case TY_ARRAY:
		fprintf(stderr, "[\033[91m%zu\033[0m]", Type.NumElements);
		DebugType(*Type.ElementType);
		break;
	}
}

usize
TypeSize(struct type Type)
{
	switch (Type.Kind) {
	case TY_DUMMY:
		Assert(false);
	case TY_I64:
		return 8;
	case TY_ARRAY:
		return TypeSize(*Type.ElementType) * Type.NumElements;
	}
}

internal void
AddTypeToExpression(struct expression *Expression)
{
	switch (Expression->Kind) {
	case EK_NUMBER:
		Expression->Type = CreateI64Type();
		break;
	case EK_VARIABLE:
		Expression->Type = Expression->Local->Type;
		break;
	case EK_CALL:
		for (usize I = 0; I < Expression->NumArguments; I++)
			AddTypeToExpression(&Expression->Arguments[I]);
		// we just don’t resolve types for calls at the moment
		Expression->Type = CreateDummyType();
		break;
	case EK_BINARY:
		AddTypeToExpression(Expression->Lhs);
		AddTypeToExpression(Expression->Rhs);
		Expression->Type = CreateI64Type();
		break;
	case EK_NOT:
		AddTypeToExpression(Expression->Lhs);
		Expression->Type = CreateI64Type();
		break;
	case EK_ADDRESS_OF:
		AddTypeToExpression(Expression->Lhs);
		Expression->Type = CreateI64Type();
		break;
	case EK_DEREFERENCE:
		AddTypeToExpression(Expression->Lhs);
		Expression->Type = CreateI64Type();
		break;
	case EK_INDEX:
		AddTypeToExpression(Expression->Array);
		AddTypeToExpression(Expression->Index);
		switch (Expression->Array->Type.Kind) {
		case TY_ARRAY:
			Expression->Type = *Expression->Array->Type.ElementType;
			break;
		default:
			Error("tried to index non-array value");
		}
		break;
	}
}

internal void
AddTypetoStatement(struct statement *Statement)
{
	switch (Statement->Kind) {
	case SK_VAR:
		break;
	case SK_SET:
		AddTypeToExpression(&Statement->Destination);
		AddTypeToExpression(&Statement->Source);
		break;
	case SK_BLOCK:
		for (usize I = 0; I < Statement->NumStatements; I++)
			AddTypetoStatement(&Statement->Statements[I]);
		break;
	case SK_EXPRESSION:
		AddTypeToExpression(&Statement->Expression);
		break;
	case SK_RETURN:
		AddTypeToExpression(&Statement->Expression);
		break;
	}
}

void
AddTypes(struct ast Ast)
{
	for (usize I = 0; I < Ast.NumFunctions; I++)
		AddTypetoStatement(&Ast.Functions[I].Body);
}
