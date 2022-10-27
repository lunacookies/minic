#include "minic.h"

void
DebugTokenKind(enum token_kind TokenKind)
{
	local_persist char *Strings[] = {
		"TK_NUMBER", "TK_IDENT", "TK_FUNC", "TK_STRUCT", "TK_IF",
		"TK_ELSE",   "TK_WHILE", "TK_VAR",  "TK_EOF",
	};
	Assert(ArrayLength(Strings) == TK__LAST);
	fprintf(stderr, "%s", Strings[TokenKind]);
}

struct token_buf {
	struct token *Tokens;
	usize Length;
	usize Capacity;
};

internal struct token_buf
CreateTokenBuf(void)
{
	struct token_buf T = {
		.Tokens = malloc(sizeof(struct token) * 8),
		.Length = 0,
		.Capacity = 8,
	};
	return T;
}

internal void
PushToken(struct token_buf *B, enum token_kind Kind, u8 *Start, u8 *End)
{
	struct token T = {
		.Kind = Kind,
		.Text = Start,
		.Length = End - Start,
	};

	if (B->Length == B->Capacity) {
		B->Capacity *= 2;
		B->Tokens =
		    realloc(B->Tokens, sizeof(struct token) * B->Capacity);
	}
	B->Tokens[B->Length] = T;
	B->Length += 1;
}

internal void
ShrinkToFit(struct token_buf *B)
{
	B->Tokens = realloc(B->Tokens, sizeof(struct token) * B->Length);
	B->Capacity = B->Length;
}

internal bool
IsWhitespace(u8 C)
{
	return C == ' ' || C == '\n' || C == '\t';
}

internal bool
IsDigit(u8 C)
{
	return C >= '0' && C <= '9';
}

internal bool
IsIdentStart(u8 C)
{
	return (C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_';
}

internal bool
IsIdentContinue(u8 C)
{
	return IsIdentStart(C) || IsDigit(C);
}

internal bool
IsEqual(struct token Token, char *Text)
{
	return memcmp(Token.Text, Text, Token.Length) == 0 &&
	       Text[Token.Length] == '\0';
}

internal void
ConvertKeywords(struct token *Tokens)
{
	for (struct token *T = Tokens; T->Kind != TK_EOF; T++) {
		if (IsEqual(*T, "func"))
			T->Kind = TK_FUNC;
		else if (IsEqual(*T, "struct"))
			T->Kind = TK_STRUCT;
		else if (IsEqual(*T, "if"))
			T->Kind = TK_IF;
		else if (IsEqual(*T, "else"))
			T->Kind = TK_ELSE;
		else if (IsEqual(*T, "while"))
			T->Kind = TK_WHILE;
		else if (IsEqual(*T, "var"))
			T->Kind = TK_VAR;
	}
}

struct token *
Tokenize(u8 *Input)
{
	struct token_buf TokenBuf = CreateTokenBuf();

	while (*Input) {
		if (IsWhitespace(*Input)) {
			Input++;
			continue;
		}

		if (IsDigit(*Input)) {
			u8 *Start = Input;
			do {
				Input++;
			} while (IsDigit(*Input));
			PushToken(&TokenBuf, TK_NUMBER, Start, Input);
		}

		if (IsIdentStart(*Input)) {
			u8 *Start = Input;
			do {
				Input++;
			} while (IsIdentContinue(*Input));
			PushToken(&TokenBuf, TK_IDENT, Start, Input);
		}
	}

	PushToken(&TokenBuf, TK_EOF, Input, Input);
	ConvertKeywords(TokenBuf.Tokens);
	ShrinkToFit(&TokenBuf);
	return TokenBuf.Tokens;
}

void
DebugToken(struct token Token)
{
	fprintf(stderr, "\033[94m");
	DebugTokenKind(Token.Kind);
	fprintf(stderr, "\033[0m:");
	fprintf(stderr, "\033[32m\"\033[92m");
	for (usize i = 0; i < Token.Length; i++)
		fprintf(stderr, "%c", Token.Text[i]);
	fprintf(stderr, "\033[32m\"\033[0m");
}

void
DebugTokens(struct token *Tokens)
{
	fprintf(stderr, "[\n");
	while (Tokens->Kind != TK_EOF) {
		fprintf(stderr, "\t");
		DebugToken(*Tokens);
		fprintf(stderr, "\n");
		Tokens++;
	}
	fprintf(stderr, "]\n");
}