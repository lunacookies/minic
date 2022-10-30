#!/bin/sh

assert() {
	expected="$1"
	input="$2"

	./minic "$input" > tmp.s
	as -o tmp.o tmp.s
	ld \
		-e _Main \
		-o tmp \
		-syslibroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
		-lSystem \
		tmp.o \

	./tmp
	actual="$?"

	if [ "$actual" = "$expected" ]; then
		echo "$input => $actual"
	else
		echo "$input => $expected expected, but got $actual"
		exit 1
	fi
}

assert 0 'func Main() { return 0; }'
assert 1 'func Main() { return 1; }'
assert 92 'func Main() { return 92; }'
