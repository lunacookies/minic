func main {
	nested := 1 + 1 + 1 + 1;
	precedence := 3 + 4 * 5 - 6;
	parens := (3 + 4) * 5 - 6;
	all := nested / (precedence - parens * 5);
}
