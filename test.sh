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
assert 10 'func Main() i64 { var X i64; set X = 10; return X; }'
assert 3 'func Main() i64 { var X i64; var Y *i64; set X=3; set Y=&X; return *Y; }'
assert 4 'func Main() i64 { var X i64; var Y *i64; set X=10; set Y=&X; set *Y=4; return X; }'
assert 6 'func Main() i64 { var X i64; var Y *i64; var Z **i64; set X=6; set Y=&X; set Z=&Y; return **Z; }'
assert 3 'func Main() i64 { var X i64; var Y i64; set X=3; set Y=10; return *(&Y + 1); }'
assert 10 'func Main() i64 { var X i64; var Y i64; set X=3; set Y=10; return *(&X - 1); }'
assert 4 'func Main() i64 { var X i64; var Y [3]i64; var Z i64; return &X - &Z; }'

# functions
assert 92 'func Main() i64 { return Magic(); } func Magic() i64 { return 92; }'
assert 9 'func Main() i64 { return Add(4, 5); } func Add(X i64, Y i64) i64 { return X+Y; }'

# arrays
assert 10 'func Main() i64 { var A [3]i64; set A[0]=5; set A[1]=10; set A[2]=15; return A[1]; }'
assert 12 'func Main() i64 { var A [3]i64; set A[1]=12; var B *[3]i64; set B=&A; return (*B)[1]; }'
assert 42 'func Main() i64 { var A [2]i64; var B [2]i64; set A[0]=5; set A[1]=42; set B=A; return B[1]; }'
assert 12 'func Main() i64 { var A [3]i64; set *(&A[1]+1)=12; return A[2]; }'
assert 3 'func Main() i64 { var A [10][10]i64; set A[8][5]=3; return A[8][5]; }'
assert 30 'func Main() i64 { var A [20][5]i64; set A[3][1]=30; var B [5]i64; set B=A[3]; return B[1]; }'

# structs
assert 2 'struct a { X i64 } func Main() i64 { var A a; set A.X=2; return A.X; }'
assert 16 'struct pair { A i64, B i64 } func Main() i64 { var Pair pair; set Pair.A=4; set Pair.B=Pair.A * 5; return Pair.B - Pair.A; }'
assert 6 'struct bar { D i64, E i64 } struct foo { A i64, B bar, C i64 } func Main() i64 { var Foo foo; set Foo.B.D=9; set *(&Foo.B.D + 1)=3; return Foo.B.D - Foo.B.E; }'
assert 31 'struct pair { A i64, B i64 } func Main() i64 { var Pairs [10]pair; set Pairs[0].A=5; set Pairs[0].B=2; set Pairs[1]=Pairs[0]; set Pairs[1].A=29; return Pairs[1].A + Pairs[0].B; }'
