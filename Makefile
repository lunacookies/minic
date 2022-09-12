all: tidy minic

tidy: minic.c
	clang-format --dry-run minic.c

minic: minic.c
	$(CC) -W -Wall -Wextra -Wpedantic -std=c99 -o minic minic.c
