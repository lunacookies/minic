#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define global_variable static
#define local_persist static
#define internal static

#define ArrayLength(Array) (sizeof((Array)) / (sizeof((Array)[0])))

#define Assert(B)                                                              \
	if (!(B))                                                              \
		Error("an internal error occurred at "                         \
		      "\033[1;4;97m%s:%d\033[0m",                              \
		      __FILE__, __LINE__);

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef size_t usize;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
typedef ssize_t isize;

// ------
// util.c
// ------

void Error(char *Fmt, ...);

// ----------
// tokenize.c
// ----------

// Don’t forget to change the strings in DebugTokenKind.
enum token_kind {
	TK_NUMBER,
	TK_IDENT,
	TK_FUNC,
	TK_STRUCT,
	TK_IF,
	TK_ELSE,
	TK_WHILE,
	TK_VAR,
	TK_EOF,
	TK__LAST,
};
void DebugTokenKind(enum token_kind TokenKind);

struct token {
	enum token_kind Kind;
	u8 *Text;
	usize Length;
};
void DebugToken(struct token Token);
void DebugTokens(struct token *Tokens);

struct token *Tokenize(u8 *Input);
