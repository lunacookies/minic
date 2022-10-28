#include "minic.h"

global_variable u32 Indentation = 0;

void
Newline(void)
{
	fprintf(stderr, "\n");
	for (u32 I = 0; I < Indentation; I++)
		fprintf(stderr, "\t");
}

void
DebugStatement(struct statement Statement)
{
	switch (Statement.Kind) {
	case SK_VAR:
		fprintf(stderr, "\033[94mvar\033[0m %s;", Statement.Name);
		return;
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
		return;
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

internal struct statement ParseStatement(void);

global_variable struct token *Current;

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
		struct statement Statement;
		Statement.Kind = SK_VAR;
		Expect(TK_VAR);
		Statement.Name = ExpectIdent();
		Expect(TK_SEMICOLON);
		return Statement;
	}
	case TK_LBRACE:
		return ParseBlock();
	default:
		Error("expected a statement");
	}
}

internal struct func
ParseFunction(void)
{
	struct func Function;
	Expect(TK_FUNC);
	Function.Name = ExpectIdent();
	Expect(TK_LPAREN);
	Expect(TK_RPAREN);
	Function.Body = ParseStatement();
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
