CFLAGS=\
	-std=c11 \
	-fshort-enums \
	-fsanitize=address \
	-g \
	-W \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wimplicit-fallthrough \
	-Wimplicit-int-conversion \
	-Wshadow \
	-Wstrict-prototypes \
	-Wmissing-prototypes

SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

all: minic tidy

minic: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJS): minic.h

tidy: *.c *.h
	clang-format -i $^

test: all
	./test.sh

clean:
	rm minic *.o

.PHONY: tidy test clean
