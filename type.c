#include "minic.h"

void
DebugType(struct type Type)
{
	switch (Type.Kind) {
	case TY_I64:
		fprintf(stderr, "\033[95mi64\033[0m");
		break;
	case TY_ARRAY:
		fprintf(stderr, "[\033[91m%zu\033[0m]", Type.NumElements);
		DebugType(*Type.ElementType);
		break;
	}
}

usize
TypeSize(struct type Type)
{
	switch (Type.Kind) {
	case TY_I64:
		return 8;
	case TY_ARRAY:
		return TypeSize(*Type.ElementType) * Type.NumElements;
	}
}
