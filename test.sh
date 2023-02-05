#!/bin/sh

mkdir test
cd test

assert() {
	expected="$1"
	input="$2"

	echo "$input" > main.mc
	../minic
	./out
	actual="$?"

	if [ "$actual" = "$expected" ]; then
		printf "%s => %s\n" "$input" "$actual"
	else
		printf "\033[31m%s => %s expected, but got %s\033[0m\n" "$input" "$expected" "$actual"
	fi
}

assert 0 'func main { return 0 }'
assert 92 'func main { return 92 }'
assert 210 'func main { return 1234 }'

assert 6 'func main { return 2+4 }'
assert 3 'func main { return 4-1 }'
assert 100 'func main { return 5*20 }'
assert 5 'func main { return 11/2 }'
assert 7 'func main { return 1+2*3 }'
assert 5 'func main { return 1*2+3 }'
assert 9 'func main { return (1+2)*3 }'
assert 5 'func main { return 1*(2+3) }'
assert 5 'func main { return 1+1+1+1+1+1-1 }'
assert 64 'func main { return 2*2*2*2*2*2 }'

assert 5 'func main { var x=5 return x }'
assert 10 'func main { var x=10 var y=x return y }'
assert 20 'func main { var x=1 set x=20 return x }'
assert 9 'func main { var x=2 set x=x+1 set x=x*3 return x }'

assert 4 'func main { if 0 return 2 else return 4 }'
assert 2 'func main { if 1 return 2 else return 4 }'

assert 1 'func main { return 0==0 }'
assert 1 'func main { return 5==5 }'
assert 1 'func main { var a=99 var b=a return a==b }'
assert 0 'func main { return 4==8 }'
assert 0 'func main { return 5!=5 }'
assert 1 'func main { return 4!=8 }'

assert 32 'func main { var x=1 var i=0 while i!=5 { set x=x*2 set i=i+1 } return x }'

cd ..
rm -r test
