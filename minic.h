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

_Noreturn void Error(char *Fmt, ...);

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
	TK_SET,
	TK_RETURN,
	TK_I64,
	TK_LBRACE,
	TK_RBRACE,
	TK_LPAREN,
	TK_RPAREN,
	TK_LSQUARE,
	TK_RSQUARE,
	TK_LANGLE_EQUAL,
	TK_RANGLE_EQUAL,
	TK_LANGLE,
	TK_RANGLE,
	TK_DOT,
	TK_COMMA,
	TK_SEMICOLON,
	TK_PLUS,
	TK_MINUS,
	TK_STAR,
	TK_SLASH,
	TK_PRETZEL,
	TK_PIPE,
	TK_SQUIGGLE,
	TK_EQUAL_EQUAL,
	TK_EQUAL,
	TK_BANG_EQUAL,
	TK_EOF,
	TK__LAST,
};
u8 *TokenKindToString(enum token_kind TokenKind);
void DebugTokenKind(enum token_kind TokenKind);

struct token {
	enum token_kind Kind;
	u8 *Text;
	usize Length;
};
void DebugToken(struct token Token);
void DebugTokens(struct token *Tokens);

struct token *Tokenize(u8 *Input);

// ------
// type.c
// ------

enum type_kind {
	TY_DUMMY,
	TY_I64,
	TY_ARRAY,
	TY_POINTER,
	TY_STRUCT,
};

struct type {
	enum type_kind Kind;

	// array
	struct type *ElementType;
	usize NumElements;

	// pointer
	struct type *Pointee;

	// struct
	u8 *Name;
	struct field *Fields;
	usize NumFields;
	usize Size;
};
void DebugType(struct type Type);
struct type CreateDummyType(void);
struct type CreateI64Type(void);
struct type CreateArrayType(struct type ElementType, usize NumElements);
struct type CreatePointerType(struct type Pointee);

usize TypeSize(struct type Type);

// -------
// parse.c
// -------

enum expression_kind {
	EK_NUMBER,
	EK_VARIABLE,
	EK_CALL,
	EK_BINARY,
	EK_NOT,
	EK_ADDRESS_OF,
	EK_DEREFERENCE,
	EK_INDEX,
	EK_FIELD_ACCESS,
};

enum statement_kind {
	SK_VAR,
	SK_SET,
	SK_BLOCK,
	SK_EXPRESSION,
	SK_RETURN,
	SK_IF,
	SK_WHILE,
};

enum binary_operator {
	OP_ADD,
	OP_SUBTRACT,
	OP_MULTIPLY,
	OP_DIVIDE,
	OP_AND,
	OP_OR,
	OP_EQUAL,
	OP_NOT_EQUAL,
	OP_LESS_THAN,
	OP_LESS_THAN_EQUAL,
};

struct local {
	u8 *Name;
	struct type Type;
	isize Offset;
};

struct field {
	u8 *Name;
	struct type Type;
	isize Offset;
};

struct expression {
	enum expression_kind Kind;
	struct type Type;

	// number
	usize Value;

	// variable
	struct local *Local;

	// call
	u8 *Name;
	struct expression *Arguments;
	usize NumArguments;

	// binary operations and unary operations (just Lhs)
	struct expression *Lhs;
	struct expression *Rhs;
	enum binary_operator Operator;

	// index
	struct expression *Array;
	struct expression *Index;

	// field access (base is in Lhs)
	struct field *Field;
};

struct statement {
	enum statement_kind Kind;

	// var
	struct local *Local;

	// set
	struct expression Destination;
	struct expression Source;

	// block
	struct statement *Statements;
	usize NumStatements;

	// expression statement, return, if and while
	struct expression Expression;

	// if
	struct statement *TrueBranch;
	struct statement *FalseBranch;

	// while
	struct statement *Body;
};

struct parameter {
	u8 *Name;
	struct type Type;
};

struct func {
	u8 *Name;
	struct parameter *Parameters;
	usize NumParameters;
	struct type ReturnType;
	struct statement Body;
	struct local *Locals;
	usize NumLocals;
	usize StackSize;
};

struct ast {
	struct func *Functions;
	usize NumFunctions;
};

void DebugExpression(struct expression Expression);
void DebugStatement(struct statement Statement);
void DebugFunction(struct func Function);
void DebugAst(struct ast Ast);
struct ast Parse(struct token *Tokens);

// actually from type.c
void AddTypeToExpression(struct expression *Expression);
void AddTypeToStatement(struct statement *Statement);
void AddTypes(struct ast Ast);

// ---------
// codegen.c
// ---------

void Codegen(struct ast Ast);
