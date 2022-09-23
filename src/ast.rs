#[derive(Debug)]
pub(crate) enum Item {
	Proc { name: String, body: Vec<Stmt> },
}

#[derive(Debug)]
pub(crate) enum Stmt {
	LocalDef { name: String, value: Expr, ty: Type },
	LocalSet { name: String, new_value: Expr },
	Loop { stmts: Vec<Stmt> },
	If { condition: Expr, true_branch: Vec<Stmt>, false_branch: Vec<Stmt> },
	Break,
	Continue,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub(crate) enum Type {
	U64,
}

#[derive(Debug)]
pub(crate) enum Expr {
	Local(String),
	Int(u64),
	Add(Box<Expr>, Box<Expr>),
	Equal(Box<Expr>, Box<Expr>),
}
