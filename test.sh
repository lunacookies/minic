#!/bin/sh

./out/minic --test

assert() {
	expected="$1"
	input="$2"

	echo "$input" > main.mc
	../out/minic
	./out
	actual="$?"

	if [ "$actual" = "$expected" ]; then
		printf "%s => %s\n" "$input" "$actual"
	else
		printf "\033[31m%s => %s expected, but got %s\033[0m\n" "$input" "$expected" "$actual"
	fi
}

tests() {
	assert 0 'func main { return 0; }'
	assert 92 'func main { return 92; }'
	assert 210 'func main { return 1234; }'

	assert 6 'func main { return 2+4; }'
	assert 3 'func main { return 4-1; }'
	assert 100 'func main { return 5*20; }'
	assert 5 'func main { return 11/2; }'
	assert 7 'func main { return 1+2*3; }'
	assert 5 'func main { return 1*2+3; }'
	assert 9 'func main { return (1+2)*3; }'
	assert 5 'func main { return 1*(2+3); }'
	assert 5 'func main { return 1+1+1+1+1+1-1; }'
	assert 64 'func main { return 2*2*2*2*2*2; }'

	assert 5 'func main { x:=5; return x; }'
	assert 10 'func main { x:=10; y:=x; return y; }'
	assert 20 'func main { x:=1; set x=20; return x; }'
	assert 9 'func main { x:=2; set x=x+1; set x=x*3; return x; }'

	assert 4 'func main { if (0) return 2; else return 4; }'
	assert 2 'func main { if (1) return 2; else return 4; }'
	assert 4 'func main { if (0) return 2; return 4; }'
	assert 2 'func main { if (1) return 2; return 4; }'

	assert 1 'func main { return 0==0; }'
	assert 1 'func main { return 5==5; }'
	assert 1 'func main { a:=99; b:=a; return a==b; }'
	assert 0 'func main { return 4==8; }'
	assert 0 'func main { return 5!=5; }'
	assert 1 'func main { return 4!=8; }'

	assert 1 'func main { return 1>0; }'
	assert 0 'func main { return 0>0; }'
	assert 0 'func main { return 0>1; }'

	assert 0 'func main { return 1<0; }'
	assert 0 'func main { return 0<0; }'
	assert 1 'func main { return 0<1; }'

	assert 1 'func main { return 1>=0; }'
	assert 1 'func main { return 0>=0; }'
	assert 0 'func main { return 0>=1; }'

	assert 0 'func main { return 1<=0; }'
	assert 1 'func main { return 0<=0; }'
	assert 1 'func main { return 0<=1; }'

	assert 32 'func main { x:=1; i:=0; while (i!=5) { set x=x*2; set i=i+1; } return x; }'

	assert 5 'func main { x:=5; return *&x; }'
	assert 40 'func main { a:=40; b:=&a; return *b; }'
	assert 10 'func main { a:=1; b:=&a; set *b = 10; return a; }'
}

mkdir test
(cd test && tests)
rm -r test
