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
	StoreConst { dst: Reg, value: Const },
	Store { dst: Reg, src: Reg },
	Br { label: Label },
	CondBr { label: Label, condition: Reg },
	Add { dst: Reg, lhs: Reg, rhs: Reg },
	CmpEq { dst: Reg, lhs: Reg, rhs: Reg },
}

#[derive(Clone, Copy)]
pub(crate) struct Reg(u32);

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) struct Label(u32);

#[derive(Clone, Copy, PartialEq, Eq)]
pub(crate) struct Const(u64);

impl Label {
	const PLACEHOLDER: Label = Label(u32::MAX);
}

pub(crate) fn lower(ast: &[Stmt]) -> Mir {
	LowerCtx {
		mir: Mir::default(),
		scopes: Vec::new(),
		current_reg: Reg(0),
		current_label: Label(0),
		loop_tops: Vec::new(),
		break_fixups: Vec::new(),
	}
	.lower(ast)
}

struct LowerCtx {
	mir: Mir,
	scopes: Vec<HashMap<String, Reg>>,
	current_reg: Reg,
	current_label: Label,
	loop_tops: Vec<Label>,
	break_fixups: Vec<Vec<usize>>,
}

impl LowerCtx {
	fn lower(mut self, ast: &[Stmt]) -> Mir {
		self.lower_block(ast);
		self.mir
	}

	fn lower_stmt(&mut self, stmt: &Stmt) {
		match stmt {
			Stmt::LocalDef { name, value } => self.lower_local_def(name, value),
			Stmt::LocalSet { name, new_value } => self.lower_local_set(name, new_value),
			Stmt::Loop { stmts } => self.lower_loop(stmts),
			Stmt::If { condition, true_branch, false_branch } => {
				self.lower_if(condition, true_branch, false_branch)
			}
			Stmt::Break => {
				self.break_fixups.last_mut().unwrap().push(self.mir.instrs.len());
				self.emit(Instr::Br { label: Label::PLACEHOLDER })
			}
			Stmt::Continue => self.emit(Instr::Br { label: *self.loop_tops.last().unwrap() }),
		}
	}

	fn lower_local_def(&mut self, name: &str, value: &Expr) {
		// every local gets its own register in case we want to mutate its value later
		let reg = match self.lower_expr(value) {
			// if lowering the local’s value resulted in just referencing an existing register,
			// we make sure to allocate a new register
			LowerExprResult::ReferenceExisting(r) => {
				let new_reg = self.next_reg();
				self.emit(Instr::Store { dst: new_reg, src: r });
				new_reg
			}
			LowerExprResult::AllocateNew(r) => r,
		};

		self.insert_in_scope(name.to_string(), reg);
	}

	fn lower_local_set(&mut self, name: &str, new_value: &Expr) {
		let local_reg = self.lookup_in_scope(name);
		let new_value_reg = self.lower_expr(new_value).reg();
		self.emit(Instr::Store { dst: local_reg, src: new_value_reg });
	}

	fn lower_loop(&mut self, stmts: &[Stmt]) {
		let top = self.next_label();

		self.loop_tops.push(top);
		self.break_fixups.push(Vec::new());

		self.lower_block(stmts);
		self.emit(Instr::Br { label: top });

		self.loop_tops.pop();
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

	fn lower_if(&mut self, condition: &Expr, true_branch: &[Stmt], false_branch: &[Stmt]) {
		let condition = self.lower_expr(condition).reg();
		let br_idx = self.mir.instrs.len();
		self.emit(Instr::CondBr { label: Label::PLACEHOLDER, condition });

		self.lower_block(false_branch);
		let skip_br_idx = self.mir.instrs.len();
		self.emit(Instr::Br { label: Label::PLACEHOLDER });

		let true_branch_top = self.next_label();
		self.lower_block(true_branch);
		let true_branch_bottom = self.next_label();

		match &mut self.mir.instrs[br_idx] {
			Instr::CondBr { label, .. } => {
				assert_eq!(*label, Label::PLACEHOLDER);
				*label = true_branch_top;
			}
			_ => unreachable!(),
		}

		match &mut self.mir.instrs[skip_br_idx] {
			Instr::Br { label } => {
				assert_eq!(*label, Label::PLACEHOLDER);
				*label = true_branch_bottom;
			}
			_ => unreachable!(),
		}
	}

	fn lower_block(&mut self, block: &[Stmt]) {
		self.scopes.push(HashMap::new());
		for stmt in block {
			self.lower_stmt(stmt);
		}
		self.scopes.pop();
	}

	fn lower_expr(&mut self, expr: &Expr) -> LowerExprResult {
		match expr {
			Expr::Local(name) => LowerExprResult::ReferenceExisting(self.lookup_in_scope(name)),
			Expr::Int(n) => {
				let dst = self.next_reg();
				self.emit(Instr::StoreConst { dst, value: Const(*n) });
				LowerExprResult::AllocateNew(dst)
			}
			Expr::Add(lhs, rhs) => {
				let lhs = self.lower_expr(lhs).reg();
				let rhs = self.lower_expr(rhs).reg();
				let dst = self.next_reg();
				self.emit(Instr::Add { dst, lhs, rhs });
				LowerExprResult::AllocateNew(dst)
			}
			Expr::Equal(lhs, rhs) => {
				let lhs = self.lower_expr(lhs).reg();
				let rhs = self.lower_expr(rhs).reg();
				let dst = self.next_reg();
				self.emit(Instr::CmpEq { dst, lhs, rhs });
				LowerExprResult::AllocateNew(dst)
			}
		}
	}

	fn emit(&mut self, instr: Instr) {
		self.mir.instrs.push(instr);
	}

	fn insert_in_scope(&mut self, name: String, reg: Reg) {
		self.scopes.last_mut().unwrap().insert(name, reg);
	}

	fn lookup_in_scope(&mut self, name: &str) -> Reg {
		for scope in self.scopes.iter().rev() {
			if let Some(reg) = scope.get(name) {
				return *reg;
			}
		}
		eprintln!("error: undefined variable `{name}`");
		std::process::exit(1)
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
				// this alignment width is larger than you’d expect
				// due to coloring escape codes
				Some(label) => write!(f, "{:>13}:", format!("{label:?}"))?,
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

enum LowerExprResult {
	ReferenceExisting(Reg),
	AllocateNew(Reg),
}

impl LowerExprResult {
	fn reg(self) -> Reg {
		match self {
			LowerExprResult::ReferenceExisting(r) => r,
			LowerExprResult::AllocateNew(r) => r,
		}
	}
}

impl fmt::Debug for Instr {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		match self {
			Self::StoreConst { dst, value } => write!(f, "{dst:?} = {value:?}"),
			Self::Store { dst, src } => write!(f, "{dst:?} = {src:?}"),
			Self::Br { label } => write!(f, "\x1b[32mbr\x1b[0m {label:?}"),
			Self::CondBr { label, condition } => {
				write!(f, "\x1b[32mcond_br\x1b[0m {label:?}, {condition:?}")
			}
			Self::Add { dst, lhs, rhs } => {
				write!(f, "{dst:?} = \x1b[32madd\x1b[0m {lhs:?}, {rhs:?}")
			}
			Self::CmpEq { dst, lhs, rhs } => {
				write!(f, "{dst:?} = \x1b[32mcmp_eq\x1b[0m {lhs:?}, {rhs:?}")
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
		write!(f, "\x1b[35m#{}\x1b[0m", self.0)
	}
}

impl fmt::Debug for Const {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		write!(f, "\x1b[36m{}\x1b[0m", self.0)
	}
}
