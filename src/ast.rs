#[derive(Debug)]
pub(crate) struct LocalDef {
	pub(crate) name: String,
	pub(crate) value: Expr,
}

#[derive(Debug)]
pub(crate) enum Expr {
	Local(String),
	Int(u64),
	Add(Box<Expr>, Box<Expr>),
}
