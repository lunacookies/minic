use crate::ast::{Expr, LocalDef};
use std::collections::HashMap;
use std::fmt;

pub(crate) enum Instr {
	StoreConst { reg: Reg, value: u64 },
	Add { dst: Reg, lhs: Reg, rhs: Reg },
}

#[derive(Clone, Copy)]
pub(crate) struct Reg(u32);

pub(crate) fn lower(ast: &[LocalDef]) -> Vec<Instr> {
	LowerCtx { instrs: Vec::new(), scope: HashMap::new(), current_reg: Reg(0) }.lower(ast)
}

struct LowerCtx {
	instrs: Vec<Instr>,
	scope: HashMap<String, Reg>,
	current_reg: Reg,
}

impl LowerCtx {
	fn lower(mut self, ast: &[LocalDef]) -> Vec<Instr> {
		for local_def in ast {
			self.lower_local_def(local_def);
		}
		self.instrs
	}

	fn lower_local_def(&mut self, local_def: &LocalDef) {
		let reg = self.lower_expr(&local_def.value);
		self.scope.insert(local_def.name.clone(), reg);
	}

	fn lower_expr(&mut self, expr: &Expr) -> Reg {
		match expr {
			Expr::Local(name) => match self.scope.get(name) {
				Some(r) => *r,
				None => {
					eprintln!("error: undefined variable `{name}`");
					std::process::exit(1)
				}
			},
			Expr::Int(n) => {
				let reg = self.next_reg();
				self.instrs.push(Instr::StoreConst { reg, value: *n });
				reg
			}
			Expr::Add(lhs, rhs) => {
				let lhs = self.lower_expr(lhs);
				let rhs = self.lower_expr(rhs);
				let dst = self.next_reg();
				self.instrs.push(Instr::Add { dst, lhs, rhs });
				dst
			}
		}
	}

	fn next_reg(&mut self) -> Reg {
		let r = self.current_reg;
		self.current_reg.0 += 1;
		r
	}
}

impl fmt::Debug for Instr {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		match self {
			Self::StoreConst { reg, value } => write!(f, "{reg:?} = \x1b[36m{value}\x1b[0m"),
			Self::Add { dst, lhs, rhs } => {
				write!(f, "{dst:?} = \x1b[32madd\x1b[0m {lhs:?}, {rhs:?}")
			}
		}
	}
}

impl fmt::Debug for Reg {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		write!(f, "\x1b[34m%{}\x1b[0m", self.0)
	}
}
