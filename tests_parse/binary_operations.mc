func main {
	var nested = 1 + 1 + 1 + 1;
	var precedence = 3 + 4 * 5 - 6;
	var parens = (3 + 4) * 5 - 6;
	var all = nested / (precedence - parens * 5);
}
