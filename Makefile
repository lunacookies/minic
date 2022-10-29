CFLAGS=-std=c11 -fsanitize=address -g -W -Wall -Wextra -Wpedantic -Wimplicit-fallthrough
SRCS=$(wildcard *.c)
OBJS=$(SRCS:.c=.o)

all: minic tidy

minic: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJS): minic.h

tidy: *.c *.h
	clang-format -i $^

test: minic
	./test.sh

clean:
	rm minic *.o

.PHONY: tidy test clean
