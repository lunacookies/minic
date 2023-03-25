func main {
	i := 0;
	while i < 10 {
		set i = i + 1;
	}

	x := 0;
	y := 0;
	a := 0;
	while x < 100 {
		while y < 100 {
			set a = a + x * y;
		}
	}
}
