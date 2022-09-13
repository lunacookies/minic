CFLAGS = -W -Wall -Wextra -Wpedantic -std=c99

all: tidy minic

tidy:
	clang-format --dry-run *.h *.c

%.o: %.c %.h
	$(CC) $(CFLAGS) -c $^

minic: minic.c vec.o bump.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm *.o *.gch minic
