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

# basics
assert 0 'func Main() i64 { return 0; }'
assert 1 'func Main() i64 { return 1; }'
assert 92 'func Main() i64 { return 92; }'

# binary operators
assert 7 'func Main() i64 { return 2 + 5; }'
assert 13 'func Main() i64 { return 3 + 5 * 2; }'
assert 16 'func Main() i64 { return (3 + 5) * 2; }'
assert 3 'func Main() i64 { return 3 + 4 - 5 * 6 / 7; }'
assert 0 'func Main() i64 { return 0 | 0; }'
assert 1 'func Main() i64 { return 0 | 1; }'
assert 1 'func Main() i64 { return 1 | 1; }'
assert 0 'func Main() i64 { return 0 & 0; }'
assert 0 'func Main() i64 { return 0 & 1; }'
assert 1 'func Main() i64 { return 1 & 1; }'
assert 1 'func Main() i64 { return 1 == 1; }'
assert 0 'func Main() i64 { return 0 == 1; }'
assert 0 'func Main() i64 { return 1 != 1; }'
assert 1 'func Main() i64 { return 0 != 1; }'
assert 1 'func Main() i64 { return 5 < 10; }'
assert 0 'func Main() i64 { return 10 < 5; }'
assert 0 'func Main() i64 { return 5 > 10; }'
assert 1 'func Main() i64 { return 10 > 5; }'
assert 1 'func Main() i64 { return 5 <= 10; }'
assert 0 'func Main() i64 { return 10 <= 5; }'
assert 0 'func Main() i64 { return 5 >= 10; }'
assert 1 'func Main() i64 { return 10 >= 5; }'
assert 0 'func Main() i64 { return 1 < 1; }'
assert 1 'func Main() i64 { return 1 <= 1; }'

# variables & pointers
assert 10 'func Main() i64 { var X; set X = 10; return X; }'
assert 3 'func Main() i64 { var X; var Y; set X=3; set Y=&X; return *Y; }'
assert 4 'func Main() i64 { var X; var Y; set X=10; set Y=&X; set *Y=4; return X; }'
assert 6 'func Main() i64 { var X; var Y; var Z; set X=6; set Y=&X; set Z=&Y; return **Z; }'

# functions
assert 92 'func Main() i64 { return Magic(); } func Magic() i64 { return 92; }'
assert 9 'func Main() i64 { return Add(4, 5); } func Add(X i64, Y i64) i64 { return X+Y; }'
