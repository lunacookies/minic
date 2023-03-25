func main {
	x := 10
	y := &x
	z := (*y + *&x)
}
