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
		error("an internal error occurred at "                         \
		      "\033[1;4;97m%s:%d\033[0m",                              \
		      __FILE__, __LINE__);

// ----------------------------------------------------------------------------
// utils.c

void error(char *fmt, ...);

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

// ----------------------------------------------------------------------------
// lexer.c

typedef enum tokenKind {
	TOK_NUMBER,
	TOK_IDENTIFIER,
	TOK_FUNC,
	TOK_RETURN
} tokenKind;

typedef struct span {
	u32 start;
	u32 end;
} span;

typedef struct token {
	tokenKind kind;
	span span;
} token;

typedef struct tokenBuffer {
	token *tokens;
	usize count;
} tokenBuffer;

tokenBuffer lex(u8 *input, memory *m);
