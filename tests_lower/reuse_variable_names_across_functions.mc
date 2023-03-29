func a {
	x := 5
}

func b {
	x0 := 5
	x := &x0
}

func c {
	x1 := 5
	x0 := &x1
	x := &x0
}