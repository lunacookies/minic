#[derive(Debug)]
pub(crate) enum Stmt {
	LocalDef { name: String, value: Expr },
	Loop { stmts: Vec<Stmt> },
}

#[derive(Debug)]
pub(crate) enum Expr {
	Local(String),
	Int(u64),
	Add(Box<Expr>, Box<Expr>),
}
