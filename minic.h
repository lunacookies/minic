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
void debugLog(char *fmt, ...);
void debugLogV(char *fmt, va_list ap);
u32 numCpus(void);
u64 rotl(u64 value, u64 count);
u64 rotr(u64 value, u64 count);
u64 fxhash(u8 *ptr, usize len);

// ----------------------------------------------------------------------------
// bump.c

typedef struct bump {
	u8 *top;
	usize bytes_used;
	usize max_size;
} bump;

typedef struct bumpMark {
	usize bytes_used;
	u8 *top;
} bumpMark;

bump createBump(void *buffer, usize size);
bumpMark markBump(bump *b);
void clearBumpToMark(bump *b, bumpMark mark);
void *allocateInBump(bump *b, usize size);
void *copyInBump(bump *b, void *buffer, usize size);
bump createSubBump(bump *b, usize size);
u8 *printfInBumpWithNull(bump *b, char *fmt, ...);
u8 *printfInBumpNoNull(bump *b, char *fmt, ...);
u8 *printfInBumpV(bump *b, bool null_terminated, char *fmt, va_list ap);

// ----------------------------------------------------------------------------
// memory.c

typedef struct memory {
	bump temp;
	bump general;
} memory;

bump allocateFromOs(usize size);
memory initMemory(void);

// ----------------------------------------------------------------------------
// test.c

typedef u8 *(*transformer)(u8 *, memory *);
void runTests(u8 *dir_name, transformer t, bump *b);

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

typedef struct diagnosticsStorage {
	u16 *files;
	span *spans;
	severity *severities;
	u32 *message_starts;
	bump all_messages;
	u16 count;
} diagnosticsStorage;

void initializeDiagnostics(memory *m);
void recordDiagnostic(severity severity, span span, char *fmt, ...);
void recordDiagnosticV(severity severity, span span, char *fmt, va_list ap);
bool anyErrors(void);
u8 *showDiagnostics(bump *b, bool color);

// ----------------------------------------------------------------------------
// lex.c

typedef enum tokenKind {
	TOK_EOF,
	TOK_ERROR,
	TOK_NUMBER,
	TOK_IDENTIFIER,
	TOK_FUNC,
	TOK_RETURN,
	TOK_VAR,
	TOK_SET,
	TOK_IF,
	TOK_ELSE,
	TOK_WHILE,
	TOK_EQUAL,
	TOK_EQUAL_EQUAL,
	TOK_BANG_EQUAL,
	TOK_PLUS,
	TOK_DASH,
	TOK_STAR,
	TOK_SLASH,
	TOK_LBRACE,
	TOK_RBRACE,
	TOK_LPAREN,
	TOK_RPAREN,
	TOK_LANGLE,
	TOK_LANGLE_EQUAL,
	TOK_RANGLE,
	TOK_RANGLE_EQUAL
} tokenKind;

typedef struct identifierId {
	u32 raw;
} identifierId;

typedef struct tokenBuffer {
	tokenKind *kinds;
	span *spans;
	identifierId *identifier_ids;
	usize count;
} tokenBuffer;

tokenBuffer lex(u8 *input, memory *m);
u8 *identifierText(tokenBuffer buf, u32 token_id);
u8 *showTokenKind(tokenKind kind);
u8 *debugTokenKind(tokenKind kind);
u8 *debugTokenBuffer(tokenBuffer buf, bump *b);
void debugPrintTokenBuffer(tokenBuffer buf, bump *b);

u8 *lexTests(u8 *input, memory *m);

// ----------------------------------------------------------------------------
// intern.c

typedef struct interner {
	u8 **contents;
} interner;

interner intern(tokenBuffer *bufs, u8 **contents, usize buf_count,
		usize identifier_count, memory *m);
u8 *lookup(interner i, identifierId id);

// ----------------------------------------------------------------------------
// parse.c

typedef struct astExpression {
	u16 index;
} astExpression;

typedef struct astStatement {
	u16 index;
} astStatement;

typedef enum astExpressionKind {
	AST_EXPR_MISSING,
	AST_EXPR_INT_LITERAL,
	AST_EXPR_VARIABLE,
	AST_EXPR_BINARY_OPERATION,
} astExpressionKind;

typedef enum astBinaryOperator {
	AST_BINOP_ADD,
	AST_BINOP_SUBTRACT,
	AST_BINOP_MULTIPLY,
	AST_BINOP_DIVIDE,
	AST_BINOP_EQUAL,
	AST_BINOP_NOT_EQUAL,
	AST_BINOP_LESS_THAN,
	AST_BINOP_LESS_THAN_EQUAL,
	AST_BINOP_GREATER_THAN,
	AST_BINOP_GREATER_THAN_EQUAL
} astBinaryOperator;

typedef struct astIntLiteral {
	u64 value;
} astIntLiteral;

typedef struct astVariable {
	identifierId name;
} astVariable;

typedef struct astBinaryOperation {
	astExpression lhs;
	astExpression rhs;
	astBinaryOperator op;
} astBinaryOperation;

typedef union astExpressionData {
	astIntLiteral int_literal;
	astVariable variable;
	astBinaryOperation binary_operation;
} astExpressionData;

typedef enum astStatementKind {
	AST_STMT_MISSING,
	AST_STMT_RETURN,
	AST_STMT_LOCAL_DEFINITION,
	AST_STMT_ASSIGN,
	AST_STMT_IF,
	AST_STMT_WHILE,
	AST_STMT_BLOCK
} astStatementKind;

typedef struct astReturn {
	astExpression value;
} astReturn;

typedef struct astLocalDefinition {
	identifierId name;
	astExpression value;
} astLocalDefinition;

typedef struct astAssign {
	astExpression lhs;
	astExpression rhs;
} astAssign;

typedef struct astIf {
	astExpression condition;
	astStatement true_branch;
	astStatement false_branch;
} astIf;

typedef struct astWhile {
	astExpression condition;
	astStatement true_branch;
} astWhile;

typedef struct astBlock {
	astStatement start;
	u16 count;
} astBlock;

typedef union astStatementData {
	astReturn retrn;
	astLocalDefinition local_definition;
	astAssign assign;
	astIf if_;
	astWhile while_;
	astBlock block;
} astStatementData;

typedef struct astFunction {
	identifierId name;
	astStatement body;
} astFunction;

typedef struct astRoot {
	astFunction *functions;

	astStatementData *statements;
	u8 *statement_kinds;
	span *statement_spans;

	astExpressionData *expressions;
	u8 *expression_kinds;
	span *expression_spans;

	u16 function_count;
	u16 statement_count;
	u16 expression_count;
} astRoot;

astRoot parse(tokenBuffer tokens, u8 *content, memory *m);
astStatementData astGetStatement(astRoot ast, astStatement statement);
astStatementKind astGetStatementKind(astRoot ast, astStatement statement);
span astGetStatementSpan(astRoot ast, astStatement statement);
astExpressionData astGetExpression(astRoot ast, astExpression expression);
astExpressionKind astGetExpressionKind(astRoot ast, astExpression expression);
span astGetExpressionSpan(astRoot ast, astExpression expression);
u8 *debugAst(astRoot ast, interner interner, bump *b);
void debugPrintAst(astRoot ast, interner interner, bump *b);

// ----------------------------------------------------------------------------
// lower.c

typedef struct hirNode {
	u16 index;
} hirNode;

typedef struct hirLocal {
	u16 index;
} hirLocal;

typedef enum hirType { HIR_TYPE_VOID, HIR_TYPE_I64 } hirType;

typedef enum hirNodeKind {
	HIR_MISSING,
	HIR_INT_LITERAL,
	HIR_VARIABLE,
	HIR_BINARY_OPERATION,
	HIR_ASSIGN,
	HIR_IF,
	HIR_WHILE,
	HIR_RETURN,
	HIR_BLOCK
} hirNodeKind;

typedef struct hirIntLiteral {
	u64 value;
} hirIntLiteral;

typedef struct hirVariable {
	hirLocal local;
} hirVariable;

typedef struct hirBinaryOperation {
	hirNode lhs;
	hirNode rhs;
	astBinaryOperator op;
} hirBinaryOperation;

typedef struct hirAssign {
	hirNode lhs;
	hirNode rhs;
} hirAssign;

typedef struct hirIf {
	hirNode condition;
	hirNode true_branch;
	hirNode false_branch;
} hirIf;

typedef struct hirWhile {
	hirNode condition;
	hirNode true_branch;
} hirWhile;

typedef struct hirReturn {
	hirNode value;
} hirReturn;

typedef struct hirBlock {
	hirNode start;
	u16 count;
} hirBlock;

typedef struct hirNodeData {
	hirIntLiteral int_literal;
	hirVariable variable;
	hirBinaryOperation binary_operation;
	hirAssign assign;
	hirIf if_;
	hirWhile while_;
	hirReturn retrn;
	hirBlock block;
} hirNodeData;

typedef struct hirFunction {
	hirLocal locals_start;
	u16 locals_count;
	hirNode body;
	identifierId name;
} hirFunction;

typedef struct hirRoot {
	hirFunction *functions;

	hirNodeData *nodes;
	hirNodeKind *node_kinds;
	hirType *node_types;
	span *node_spans;

	identifierId *local_names;
	hirType *local_types;

	u16 function_count;
	u16 node_count;
	u16 local_count;

	hirLocal current_function_locals_start;
} hirRoot;

hirRoot lower(astRoot ast, memory *m);
hirNodeData hirGetNode(hirRoot hir, hirNode node);
hirNodeKind hirGetNodeKind(hirRoot hir, hirNode node);
hirType hirGetNodeType(hirRoot hir, hirNode node);
span hirGetNodeSpan(hirRoot hir, hirNode node);
identifierId hirGetLocalName(hirRoot hir, hirLocal local);
hirType hirGetLocalType(hirRoot hir, hirLocal local);
u32 typeSize(hirType type);
u8 *debugHirType(hirType type);
u8 *debugHir(hirRoot hir, interner interner, bump *b);
void debugPrintHir(hirRoot hir, interner interner, bump *b);

// ----------------------------------------------------------------------------
// codegen.c

void codegen(hirRoot hir, interner interner, bump *assembly, memory *m);
