#include "minic.h"

global_variable u32 Indentation = 0;

internal void
Newline(void)
{
	fprintf(stderr, "\n");
	for (u32 I = 0; I < Indentation; I++)
		fprintf(stderr, "\t");
}

internal void
DebugBinaryOperator(enum binary_operator Operator)
{
	fprintf(stderr, "\033[94m");
	switch (Operator) {
	case OP_ADD:
		fprintf(stderr, "+");
		break;
	case OP_SUBTRACT:
		fprintf(stderr, "-");
		break;
	case OP_MULTIPLY:
		fprintf(stderr, "*");
		break;
	case OP_DIVIDE:
		fprintf(stderr, "/");
		break;
	case OP_AND:
		fprintf(stderr, "&");
		break;
	case OP_OR:
		fprintf(stderr, "|");
		break;
	case OP_EQUAL:
		fprintf(stderr, "==");
		break;
	case OP_NOT_EQUAL:
		fprintf(stderr, "!=");
		break;
	case OP_LESS_THAN:
		fprintf(stderr, "<");
		break;
	case OP_LESS_THAN_EQUAL:
		fprintf(stderr, "<=");
		break;
	}
	fprintf(stderr, "\033[0m");
}

void
DebugExpression(struct expression Expression)
{
	switch (Expression.Kind) {
	case EK_NUMBER:
		fprintf(stderr, "\033[91m%zu\033[0m", Expression.Value);
		break;
	case EK_VARIABLE:
		fprintf(stderr, "%s", Expression.Local->Name);
		break;
	case EK_CALL:
		fprintf(stderr, "\033[93m%s\033[0m", Expression.Name);
		fprintf(stderr, "(");
		for (usize I = 0; I < Expression.NumArguments; I++) {
			if (I != 0)
				fprintf(stderr, ", ");
			DebugExpression(Expression.Arguments[I]);
		}
		fprintf(stderr, ")");
		break;
	case EK_BINARY:
		fprintf(stderr, "(");
		DebugExpression(*Expression.Lhs);
		fprintf(stderr, " ");
		DebugBinaryOperator(Expression.Operator);
		fprintf(stderr, " ");
		DebugExpression(*Expression.Rhs);
		fprintf(stderr, ")");
		break;
	case EK_NOT:
		fprintf(stderr, "\033[94m~\033[0m");
		DebugExpression(*Expression.Lhs);
		break;
	case EK_ADDRESS_OF:
		fprintf(stderr, "\033[94m&\033[0m");
		DebugExpression(*Expression.Lhs);
		break;
	case EK_DEREFERENCE:
		fprintf(stderr, "\033[94m*\033[0m");
		DebugExpression(*Expression.Lhs);
		break;
	}
}

void
DebugStatement(struct statement Statement)
{
	switch (Statement.Kind) {
	case SK_VAR:
		fprintf(stderr,
		        "\033[94mvar\033[0m %s; \033[90m# size: %zu\033[0m",
		        Statement.Local->Name, Statement.Local->Size);
		break;
	case SK_SET:
		fprintf(stderr, "\033[94mset\033[0m ");
		DebugExpression(Statement.Destination);
		fprintf(stderr, " = ");
		DebugExpression(Statement.Source);
		fprintf(stderr, ";");
		break;
	case SK_BLOCK:
		fprintf(stderr, "{");
		Indentation++;
		for (usize I = 0; I < Statement.NumStatements; I++) {
			Newline();
			DebugStatement(Statement.Statements[I]);
		}
		Indentation--;
		Newline();
		fprintf(stderr, "}");
		break;
	case SK_EXPRESSION:
		DebugExpression(Statement.Expression);
		fprintf(stderr, ";");
		break;
	case SK_RETURN:
		fprintf(stderr, "\033[94mreturn\033[0m ");
		DebugExpression(Statement.Expression);
		fprintf(stderr, ";");
		break;
	}
}

void
DebugFunction(struct func Function)
{
	fprintf(stderr, "\033[94mfunc\033[0m ");
	fprintf(stderr, "\033[93m%s\033[0m", Function.Name);
	fprintf(stderr, "() ");
	DebugStatement(Function.Body);
	Newline();
}

void
DebugAst(struct ast Ast)
{
	for (usize I = 0; I < Ast.NumFunctions; I++) {
		if (I != 0)
			Newline();
		DebugFunction(Ast.Functions[I]);
	}
}

internal struct expression ParseExpression(void);
internal struct statement ParseStatement(void);

global_variable struct token *Current;
global_variable struct local *Locals;
global_variable usize NumLocals;
global_variable usize LocalsCapacity;

void
InitLocals(void)
{
	LocalsCapacity = 8;
	Locals = calloc(LocalsCapacity, sizeof(struct local));
	NumLocals = 0;
}

struct local *
PushLocal(struct local Local)
{
	if (NumLocals == LocalsCapacity) {
		LocalsCapacity *= 2;
		Locals = realloc(Locals, sizeof(struct local) * LocalsCapacity);
	}
	struct local *Ptr = Locals + NumLocals;
	*Ptr = Local;
	NumLocals++;
	return Ptr;
}

struct local *
LookupLocal(u8 *Name)
{
	for (usize I = 0; I < NumLocals; I++) {
		struct local *Local = &Locals[I];
		if (strcmp((char *)Local->Name, (char *)Name) == 0)
			return Local;
	}

	Error("undefined variable `%s`", Name);
}

internal void
Expect(enum token_kind Kind)
{
	if (Current->Kind == Kind)
		Current++;
	else
		Error("expected %s but found %s", TokenKindToString(Kind),
		      TokenKindToString(Current->Kind));
}

internal u8 *
ExpectIdent(void)
{
	Expect(TK_IDENT);
	struct token IdentToken = *(Current - 1);
	return (u8 *)strndup((char *)IdentToken.Text, IdentToken.Length);
}

internal struct expression
ParseCall(void)
{
	struct expression Expression;
	Expression.Kind = EK_CALL;
	Expression.Name = ExpectIdent();

	usize Capacity = 2;
	Expression.Arguments = calloc(Capacity, sizeof(struct expression));
	Expression.NumArguments = 0;
	Expect(TK_LPAREN);
	while (Current->Kind != TK_RPAREN) {
		if (Expression.NumArguments == Capacity) {
			Capacity *= 2;
			Expression.Arguments =
			    realloc(Expression.Arguments,
			            sizeof(struct expression) * Capacity);
		}
		Expression.Arguments[Expression.NumArguments] =
		    ParseExpression();
		Expression.NumArguments++;

		if (Current->Kind != TK_RPAREN)
			Expect(TK_COMMA);
	}
	Expect(TK_RPAREN);

	return Expression;
}

internal struct expression
ParseLhs(void)
{
	switch (Current->Kind) {
	case TK_NUMBER: {
		struct expression Expression;
		Expression.Kind = EK_NUMBER;
		Expression.Value = strtol((char *)Current->Text, NULL, 10);
		Current++;
		return Expression;
	}
	case TK_IDENT: {
		if ((Current + 1)->Kind == TK_LPAREN)
			return ParseCall();
		struct expression Expression;
		Expression.Kind = EK_VARIABLE;
		Expression.Local = LookupLocal(ExpectIdent());
		return Expression;
	}
	case TK_LPAREN: {
		Expect(TK_LPAREN);
		struct expression Inner = ParseExpression();
		Expect(TK_RPAREN);
		return Inner;
	}
	case TK_SQUIGGLE: {
		Expect(TK_SQUIGGLE);
		struct expression Expression;
		Expression.Kind = EK_NOT;
		Expression.Lhs = malloc(sizeof(struct expression));
		*Expression.Lhs = ParseLhs();
		return Expression;
	}
	case TK_PRETZEL: {
		Expect(TK_PRETZEL);
		struct expression Expression;
		Expression.Kind = EK_ADDRESS_OF;
		Expression.Lhs = malloc(sizeof(struct expression));
		*Expression.Lhs = ParseLhs();
		return Expression;
	}
	case TK_STAR: {
		Expect(TK_STAR);
		struct expression Expression;
		Expression.Kind = EK_DEREFERENCE;
		Expression.Lhs = malloc(sizeof(struct expression));
		*Expression.Lhs = ParseLhs();
		return Expression;
	}
	default:
		Error("expected expression but found %s",
		      TokenKindToString(Current->Kind));
	}
}

internal u8
OperatorBindingPower(enum binary_operator Operator)
{
	switch (Operator) {
	case OP_ADD:
	case OP_SUBTRACT:
		return 3;
	case OP_MULTIPLY:
	case OP_DIVIDE:
		return 4;
	case OP_AND:
	case OP_OR:
		return 1;
	case OP_EQUAL:
	case OP_NOT_EQUAL:
	case OP_LESS_THAN:
	case OP_LESS_THAN_EQUAL:
		return 2;
	}
}

internal struct expression
ParseExpressionBindingPower(u8 MinBindingPower)
{
	struct expression Lhs = ParseLhs();

	for (;;) {
		bool FlipOperands = false;
		enum binary_operator Operator;
		switch (Current->Kind) {
		case TK_PLUS:
			Operator = OP_ADD;
			break;
		case TK_MINUS:
			Operator = OP_SUBTRACT;
			break;
		case TK_STAR:
			Operator = OP_MULTIPLY;
			break;
		case TK_SLASH:
			Operator = OP_DIVIDE;
			break;
		case TK_PRETZEL:
			Operator = OP_AND;
			break;
		case TK_PIPE:
			Operator = OP_OR;
			break;
		case TK_EQUAL_EQUAL:
			Operator = OP_EQUAL;
			break;
		case TK_BANG_EQUAL:
			Operator = OP_NOT_EQUAL;
			break;
		case TK_LANGLE:
			Operator = OP_LESS_THAN;
			break;
		case TK_RANGLE:
			Operator = OP_LESS_THAN;
			FlipOperands = true;
			break;
		case TK_LANGLE_EQUAL:
			Operator = OP_LESS_THAN_EQUAL;
			break;
		case TK_RANGLE_EQUAL:
			Operator = OP_LESS_THAN_EQUAL;
			FlipOperands = true;
			break;
		default:
			goto ExitLoop;
		}

		u8 BindingPower = OperatorBindingPower(Operator);
		if (BindingPower < MinBindingPower)
			break;

		Current++;

		struct expression Rhs =
		    ParseExpressionBindingPower(BindingPower + 1);

		if (FlipOperands) {
			struct expression Tmp = Rhs;
			Rhs = Lhs;
			Lhs = Tmp;
		}

		struct expression NewLhs;
		NewLhs.Kind = EK_BINARY;
		NewLhs.Operator = Operator;
		NewLhs.Lhs = malloc(sizeof(struct expression));
		*NewLhs.Lhs = Lhs;
		NewLhs.Rhs = malloc(sizeof(struct expression));
		*NewLhs.Rhs = Rhs;

		Lhs = NewLhs;
	}

ExitLoop:

	return Lhs;
}

internal struct expression
ParseExpression(void)
{
	return ParseExpressionBindingPower(0);
}

internal struct statement
ParseBlock(void)
{
	struct statement Statement;
	Statement.Kind = SK_BLOCK;
	usize Capacity = 2;
	Statement.Statements = calloc(Capacity, sizeof(struct statement));
	Statement.NumStatements = 0;

	Expect(TK_LBRACE);

	while (Current->Kind != TK_RBRACE) {
		struct statement S = ParseStatement();
		if (Statement.NumStatements == Capacity) {
			Capacity *= 2;
			Statement.Statements =
			    realloc(Statement.Statements,
			            sizeof(struct statement) * Capacity);
		}
		Statement.Statements[Statement.NumStatements] = S;
		Statement.NumStatements++;
	}

	Expect(TK_RBRACE);

	return Statement;
}

internal struct statement
ParseStatement(void)
{
	switch (Current->Kind) {
	case TK_VAR: {
		Expect(TK_VAR);
		u8 *Name = ExpectIdent();
		Expect(TK_SEMICOLON);

		struct statement Statement;
		Statement.Kind = SK_VAR;

		struct local Local = {
			.Name = Name,
			.Size = 8,
		};
		Statement.Local = PushLocal(Local);

		return Statement;
	}
	case TK_SET: {
		struct statement Statement;
		Statement.Kind = SK_SET;
		Expect(TK_SET);
		Statement.Destination = ParseExpression();
		Expect(TK_EQUAL);
		Statement.Source = ParseExpression();
		Expect(TK_SEMICOLON);
		return Statement;
	}
	case TK_LBRACE:
		return ParseBlock();
	case TK_RETURN: {
		struct statement Statement;
		Statement.Kind = SK_RETURN;
		Expect(TK_RETURN);
		Statement.Expression = ParseExpression();
		Expect(TK_SEMICOLON);
		return Statement;
	}
	default: {
		struct statement Statement;
		Statement.Kind = SK_EXPRESSION;
		Statement.Expression = ParseExpression();
		Expect(TK_SEMICOLON);
		return Statement;
	}
	}
}

internal struct func
ParseFunction(void)
{
	InitLocals();

	struct func Function;
	Expect(TK_FUNC);
	Function.Name = ExpectIdent();
	Expect(TK_LPAREN);
	Expect(TK_RPAREN);
	Function.Body = ParseStatement();

	Function.Locals = Locals;
	Function.NumLocals = NumLocals;

	return Function;
}

struct ast
Parse(struct token *Tokens)
{
	Current = Tokens;
	usize AstCapacity = 8;
	struct ast Ast = {
		.Functions = calloc(AstCapacity, sizeof(struct func)),
		.NumFunctions = 0,
	};

	while (Current->Kind != TK_EOF) {
		struct func Function = ParseFunction();
		if (Ast.NumFunctions == AstCapacity) {
			AstCapacity *= 2;
			Ast.Functions = realloc(
			    Ast.Functions, sizeof(struct func) * AstCapacity);
		}
		Ast.Functions[Ast.NumFunctions] = Function;
		Ast.NumFunctions++;
	}

	return Ast;
}
