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
assert 7 'func Main() { return 2 + 5; }'
assert 13 'func Main() { return 3 + 5 * 2; }'
assert 16 'func Main() { return (3 + 5) * 2; }'
assert 3 'func Main() { return 3 + 4 - 5 * 6 / 7; }'
assert 0 'func Main() { return 0 | 0; }'
assert 1 'func Main() { return 0 | 1; }'
assert 1 'func Main() { return 1 | 1; }'
assert 0 'func Main() { return 0 & 0; }'
assert 0 'func Main() { return 0 & 1; }'
assert 1 'func Main() { return 1 & 1; }'
assert 1 'func Main() { return 1 == 1; }'
assert 0 'func Main() { return 0 == 1; }'
assert 0 'func Main() { return 1 != 1; }'
assert 1 'func Main() { return 0 != 1; }'
assert 1 'func Main() { return 5 < 10; }'
assert 0 'func Main() { return 10 < 5; }'
assert 0 'func Main() { return 5 > 10; }'
assert 1 'func Main() { return 10 > 5; }'
assert 1 'func Main() { return 5 <= 10; }'
assert 0 'func Main() { return 10 <= 5; }'
assert 0 'func Main() { return 5 >= 10; }'
assert 1 'func Main() { return 10 >= 5; }'
assert 0 'func Main() { return 1 < 1; }'
assert 1 'func Main() { return 1 <= 1; }'
assert 10 'func Main() { var x; set x = 10; return x; }'
assert 3 'func Main() { var x; var y; set x=3; set y=&x; return *y; }'
assert 4 'func Main() { var x; var y; set x=10; set y=&x; set *y=4; return x; }'
assert 6 'func Main() { var x; var y; var z; set x=6; set y=&x; set z=&y; return **z; }'
