use crate::ast::{Expr, Stmt};
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt;

#[derive(Default)]
pub(crate) struct Mir {
	pub(crate) instrs: Vec<Instr>,
	pub(crate) labels: HashMap<usize, Label>,
}

pub(crate) enum Instr {
	StoreConst { reg: Reg, value: u64 },
	Br { label: Label },
	Add { dst: Reg, lhs: Reg, rhs: Reg },
}

#[derive(Clone, Copy)]
pub(crate) struct Reg(u32);

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) struct Label(u32);

impl Label {
	const PLACEHOLDER: Label = Label(u32::MAX);
}

pub(crate) fn lower(ast: &[Stmt]) -> Mir {
	LowerCtx {
		mir: Mir::default(),
		scope: HashMap::new(),
		current_reg: Reg(0),
		current_label: Label(0),
		break_fixups: Vec::new(),
	}
	.lower(ast)
}

struct LowerCtx {
	mir: Mir,
	scope: HashMap<String, Reg>,
	current_reg: Reg,
	current_label: Label,
	break_fixups: Vec<Vec<usize>>,
}

impl LowerCtx {
	fn lower(mut self, ast: &[Stmt]) -> Mir {
		for stmt in ast {
			self.lower_stmt(stmt);
		}
		self.mir
	}

	fn lower_stmt(&mut self, stmt: &Stmt) {
		match stmt {
			Stmt::LocalDef { name, value } => self.lower_local_def(name, value),
			Stmt::Loop { stmts } => self.lower_loop(stmts),
			Stmt::Break => {
				self.break_fixups.last_mut().unwrap().push(self.mir.instrs.len());
				self.emit(Instr::Br { label: Label::PLACEHOLDER })
			}
		}
	}

	fn lower_local_def(&mut self, name: &str, value: &Expr) {
		let reg = self.lower_expr(value);
		self.scope.insert(name.to_string(), reg);
	}

	fn lower_loop(&mut self, stmts: &[Stmt]) {
		self.break_fixups.push(Vec::new());

		let top = self.next_label();
		for stmt in stmts {
			self.lower_stmt(stmt);
		}
		self.emit(Instr::Br { label: top });

		let fixups = self.break_fixups.pop().unwrap();

		// avoid generating label at bottom of loop if we don’t have any breaks
		if fixups.is_empty() {
			return;
		}
		let bottom = self.next_label();

		for idx in fixups {
			match &mut self.mir.instrs[idx] {
				Instr::Br { label } => {
					assert_eq!(*label, Label::PLACEHOLDER);
					*label = bottom;
				}
				_ => unreachable!(),
			}
		}
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
				self.emit(Instr::StoreConst { reg, value: *n });
				reg
			}
			Expr::Add(lhs, rhs) => {
				let lhs = self.lower_expr(lhs);
				let rhs = self.lower_expr(rhs);
				let dst = self.next_reg();
				self.emit(Instr::Add { dst, lhs, rhs });
				dst
			}
		}
	}

	fn emit(&mut self, instr: Instr) {
		self.mir.instrs.push(instr);
	}

	fn next_reg(&mut self) -> Reg {
		let r = self.current_reg;
		self.current_reg.0 += 1;
		r
	}

	fn next_label(&mut self) -> Label {
		match self.mir.labels.entry(self.mir.instrs.len()) {
			Entry::Occupied(e) => *e.get(),
			Entry::Vacant(e) => {
				let l = self.current_label;
				e.insert(l);
				self.current_label.0 += 1;
				l
			}
		}
	}
}

impl fmt::Debug for Mir {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		writeln!(f, "MIR[[")?;

		for (i, instr) in self.instrs.iter().enumerate() {
			write!(f, "    ")?;
			match self.labels.get(&i) {
				Some(label) => write!(f, "{:?}:", label)?,
				None => write!(f, "     ")?,
			}
			writeln!(f, " {instr:?}")?;
		}

		if let Some(label) = self.labels.get(&self.instrs.len()) {
			writeln!(f, "    {label:?}:")?;
		}

		write!(f, "]]")?;
		Ok(())
	}
}

impl fmt::Debug for Instr {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		match self {
			Self::StoreConst { reg, value } => write!(f, "{reg:?} = \x1b[36m{value}\x1b[0m"),
			Self::Br { label } => write!(f, "\x1b[32mbr\x1b[0m {label:?}"),
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

impl fmt::Debug for Label {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		write!(f, "\x1b[35m#{:03}\x1b[0m", self.0)
	}
}
