#include "minic.h"

void
DebugFunction(struct func Function)
{
	fprintf(stderr, "\033[94mfunc\033[0m ");
	fprintf(stderr, "\033[93m%s\033[0m", Function.Name);
	fprintf(stderr, "() {}\n");
}

void
DebugAst(struct ast Ast)
{
	for (usize i = 0; i < Ast.NumFunctions; i++) {
		if (i != 0)
			fprintf(stderr, "\n");
		DebugFunction(Ast.Functions[i]);
	}
}

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

internal struct func
Function(void)
{
	struct func Function;
	Expect(TK_FUNC);
	Function.Name = ExpectIdent();
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
		struct func Func = Function();
		if (Ast.NumFunctions == AstCapacity) {
			AstCapacity *= 2;
			Ast.Functions = realloc(
			    Ast.Functions, sizeof(struct func) * AstCapacity);
		}
		Ast.Functions[Ast.NumFunctions] = Func;
		Ast.NumFunctions++;
	}

	return Ast;
}
