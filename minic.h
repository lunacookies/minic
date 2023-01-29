#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>

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

#define assert(B)                                                              \
	if (!(B))                                                              \
		internalError("an assert failed at %s:%d", __FILE__, __LINE__);

// ----------------------------------------------------------------------------
// utils.c

void internalError(char *fmt, ...);
void internalErrorV(char *fmt, va_list ap);
u32 numCpus(void);

// ----------------------------------------------------------------------------
// bump.c

typedef struct bump {
	u8 *top;
	usize bytes_used;
	usize max_size;
} bump;

bump createBump(void *buffer, usize size);
void clearBump(bump *b);
void *allocateInBump(bump *b, usize size);

// ----------------------------------------------------------------------------
// memory.c

typedef struct memory {
	bump temp;
	bump general;
} memory;

memory initMemory(void);

// ----------------------------------------------------------------------------
// project.c

typedef struct projectSpec {
	u16 num_files;
	u8 **file_names;
	u8 **file_contents;
} projectSpec;

projectSpec discoverProject(memory *m);

void setCurrentProject(projectSpec p);
projectSpec currentProject(void);
void setCurrentFile(u16 f);
u16 currentFile(void);

// ----------------------------------------------------------------------------
// diagnostics.c

typedef enum severity { DIAG_WARNING, DIAG_ERROR } severity;

typedef struct span {
	u32 start;
	u32 end;
} span;

void initializeDiagnosticSink(void);
void sendDiagnosticToSink(severity severity, span span, char *fmt, ...);
void sendDiagnosticToSinkV(severity severity, span span, char *fmt, va_list ap);

// ----------------------------------------------------------------------------
// lexer.c

typedef enum tokenKind {
	TOK_EOF,
	TOK_ERROR,
	TOK_NUMBER,
	TOK_IDENTIFIER,
	TOK_FUNC,
	TOK_RETURN,
	TOK_VAR,
	TOK_EQUAL
} tokenKind;

typedef struct token {
	tokenKind kind;
	span span;
} token;

typedef struct tokenBuffer {
	token *tokens;
	usize count;
} tokenBuffer;

tokenBuffer lex(u8 *input, memory *m);

// ----------------------------------------------------------------------------
// parser.c

typedef enum astExpressionKind {
	AST_EXPR_MISSING,
	AST_EXPR_INT_LITERAL
} astExpressionKind;

typedef struct astExpression {
	astExpressionKind kind;

	// int literal
	u64 value;
} astExpression;

typedef enum astStatementKind {
	AST_STMT_MISSING,
	AST_STMT_RETURN,
	AST_STMT_LOCAL_DEFINITION
} astStatementKind;

typedef struct astStatement {
	astStatementKind kind;

	// return and local definition
	astExpression *value;

	// local definition
	u8 *name;
} astStatement;

typedef struct astFunction {
	u8 *name;
	astStatement *body;
	struct astFunction *next;
} astFunction;

typedef struct astRoot {
	astFunction *functions;
} astRoot;

astRoot parse(tokenBuffer tokens, u8 *content, memory *m);
void debugAst(astRoot ast);
