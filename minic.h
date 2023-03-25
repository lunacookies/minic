#include <dirent.h>
#include <fcntl.h>
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

void internalError(const char *fmt, ...);
void internalErrorV(const char *fmt, va_list ap);
void debugLog(const char *fmt, ...);
void debugLogV(const char *fmt, va_list ap);
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
	usize padding_bytes_used;
	u32 array_builder_nesting_level;
} bump;

typedef struct bumpMark {
	usize bytes_used;
	u8 *top;
} bumpMark;

typedef struct arrayBuilder {
	bump *b;
	void *top;
	usize element_size;
} arrayBuilder;

bump bumpCreate(void *buffer, usize size);
bumpMark bumpCreateMark(bump *b);
void bumpClearToMark(bump *b, bumpMark mark);
void *bumpAllocateArray_(bump *b, usize count, usize element_size);
void *bumpCopyArray_(bump *b, void *buffer, usize count, usize element_size);
bump bumpCreateSubBump(bump *b, usize size);
char *bumpPrintf(bump *b, const char *fmt, ...);
char *bumpPrintfV(bump *b, const char *fmt, va_list ap);

#define bumpAllocateArray(type, b, count)                                      \
	((type *)(bumpAllocateArray_((b), (count), sizeof(type))))
#define bumpCopyArray(type, b, buffer, count)                                  \
	((type *)(bumpCopyArray_((b), (buffer), (count), sizeof(type))))

arrayBuilder bumpStartArrayBuilder(bump *b, usize element_size);
void arrayBuilderPush(arrayBuilder *ab, void *element);
void *bumpFinishArrayBuilder(bump *b, arrayBuilder *ab);

// ----------------------------------------------------------------------------
// string_builder.c

typedef struct stringBuilder {
	bump *bump;
	char *s;
	usize previous_bytes_used;
} stringBuilder;

stringBuilder stringBuilderCreate(bump *b);
void stringBuilderPrintf(stringBuilder *sb, const char *fmt, ...);
void stringBuilderPrintfV(stringBuilder *sb, const char *fmt, va_list ap);
char *stringBuilderFinish(stringBuilder sb);

// ----------------------------------------------------------------------------
// memory.c

typedef struct memory {
	bump temp;
	bump general;
} memory;

bump allocateFromOs(usize size);
memory memoryCreate(void);

// ----------------------------------------------------------------------------
// test.c

typedef char *(*transformer)(char *, memory *);
void runTests(const char *dir_name, transformer t, bump *b);

// ----------------------------------------------------------------------------
// project.c

typedef struct projectSpec {
	u16 num_files;
	char **file_names;
	char **file_contents;
} projectSpec;

projectSpec projectDiscover(memory *m);

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

diagnosticsStorage diagnosticsStorageCreate(bump *b);
void diagnosticsStorageRecord(diagnosticsStorage *diagnostics,
			      severity severity, span span, const char *fmt,
			      ...);
void diagnosticsStorageRecordV(diagnosticsStorage *diagnostics,
			       severity severity, span span, const char *fmt,
			       va_list ap);
void diagnosticsStorageShow(diagnosticsStorage diagnostics, stringBuilder *sb);
void diagnosticsStorageDebug(diagnosticsStorage diagnostics, stringBuilder *sb);

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
	TOK_RANGLE_EQUAL,
	TOK_COLON,
	TOK_COLON_EQUAL,
	TOK_SEMI,
	TOK_AMPERSAND
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

tokenBuffer lex(char *input, diagnosticsStorage *diagnostics, memory *m);
char *tokenBufferIdentifierText(tokenBuffer buf, u32 token_id);
const char *tokenKindShow(tokenKind kind);
const char *tokenKindDebug(tokenKind kind);
void tokenBufferDebug(tokenBuffer buf, stringBuilder *sb);
void tokenBufferDebugPrint(tokenBuffer buf, bump *b);

char *lexTests(char *input, memory *m);

// ----------------------------------------------------------------------------
// intern.c

typedef struct interner {
	char **contents;
} interner;

interner intern(tokenBuffer *bufs, char **contents, usize buf_count, memory *m);
char *internerLookup(interner i, identifierId id);

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
	AST_EXPR_ADDRESS_OF,
	AST_EXPR_DEREFERENCE
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

typedef struct astAddressOf {
	astExpression value;
} astAddressOf;

typedef struct astDereference {
	astExpression value;
} astDereference;

typedef union astExpressionData {
	astIntLiteral int_literal;
	astVariable variable;
	astBinaryOperation binary_operation;
	astAddressOf address_of;
	astDereference dereference;
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
	astStatement true_block;
	astStatement false_block;
} astIf;

typedef struct astWhile {
	astExpression condition;
	astStatement true_block;
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
	astStatementKind *statement_kinds;
	span *statement_spans;

	astExpressionData *expressions;
	astExpressionKind *expression_kinds;
	span *expression_spans;

	u16 function_count;
	u16 statement_count;
	u16 expression_count;
} astRoot;

astRoot parse(tokenBuffer tokens, char *content,
	      diagnosticsStorage *diagnostics, memory *m);
astStatementData astGetStatement(astRoot ast, astStatement statement);
astStatementKind astGetStatementKind(astRoot ast, astStatement statement);
span astGetStatementSpan(astRoot ast, astStatement statement);
astExpressionData astGetExpression(astRoot ast, astExpression expression);
astExpressionKind astGetExpressionKind(astRoot ast, astExpression expression);
span astGetExpressionSpan(astRoot ast, astExpression expression);
void astDebug(astRoot ast, interner interner, stringBuilder *sb);
void astDebugPrint(astRoot ast, interner interner, bump *b);

char *parseTests(char *input, memory *m);

// ----------------------------------------------------------------------------
// lower.c

typedef struct hirNode {
	u16 index;
} hirNode;

typedef struct hirLocal {
	u16 index;
} hirLocal;

typedef struct hirType {
	u16 index;
} hirType;

typedef enum hirNodeKind {
	HIR_MISSING,
	HIR_INT_LITERAL,
	HIR_VARIABLE,
	HIR_BINARY_OPERATION,
	HIR_ADDRESS_OF,
	HIR_DEREFERENCE,
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

typedef struct hirAddressOf {
	hirNode value;
} hirAddressOf;

typedef struct hirDereference {
	hirNode value;
} hirDereference;

typedef struct hirAssign {
	hirNode lhs;
	hirNode rhs;
} hirAssign;

typedef struct hirIf {
	hirNode condition;
	hirNode true_block;
	hirNode false_block;
} hirIf;

typedef struct hirWhile {
	hirNode condition;
	hirNode true_block;
} hirWhile;

typedef struct hirReturn {
	hirNode value;
} hirReturn;

typedef struct hirBlock {
	hirNode start;
	u16 count;
} hirBlock;

typedef union hirNodeData {
	hirIntLiteral int_literal;
	hirVariable variable;
	hirBinaryOperation binary_operation;
	hirAddressOf address_of;
	hirDereference dereference;
	hirAssign assign;
	hirIf if_;
	hirWhile while_;
	hirReturn retrn;
	hirBlock block;
} hirNodeData;

typedef enum hirTypeKind {
	HIR_TYPE_VOID,
	HIR_TYPE_I64,
	HIR_TYPE_POINTER
} hirTypeKind;

typedef struct hirPointer {
	hirType child_type;
} hirPointer;

typedef union hirTypeData {
	hirPointer pointer;
} hirTypeData;

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
	span *local_spans;

	hirTypeData *types;
	hirTypeKind *type_kinds;

	u16 function_count;
	u16 node_count;
	u16 local_count;
	u16 type_count;

	hirLocal current_function_locals_start;
} hirRoot;

hirRoot lower(astRoot ast, diagnosticsStorage *diagnostics, memory *m);
hirNodeData hirGetNode(hirRoot hir, hirNode node);
hirNodeKind hirGetNodeKind(hirRoot hir, hirNode node);
hirType hirGetNodeType(hirRoot hir, hirNode node);
span hirGetNodeSpan(hirRoot hir, hirNode node);
identifierId hirGetLocalName(hirRoot hir, hirLocal local);
hirType hirGetLocalType(hirRoot hir, hirLocal local);
span hirGetLocalSpan(hirRoot hir, hirLocal local);
hirTypeData hirGetType(hirRoot hir, hirType type);
hirTypeKind hirGetTypeKind(hirRoot hir, hirType type);
u32 hirTypeSize(hirRoot hir, hirType type);
void hirTypeDebug(hirRoot hir, hirType type, stringBuilder *sb);
void hirDebug(hirRoot hir, interner interner, stringBuilder *sb);
void hirDebugPrint(hirRoot hir, interner interner, bump *b);

char *lowerTests(char *input, memory *m);

// ----------------------------------------------------------------------------
// codegen.c

void codegen(hirRoot hir, interner interner, stringBuilder *assembly,
	     diagnosticsStorage *diagnostics, memory *m);
