use crate::ast::{Expr, Item, Stmt, Type};
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::fmt;

#[derive(Default)]
pub(crate) struct Mir {
	pub(crate) bodies: HashMap<String, Body>,
}

#[derive(Default)]
pub(crate) struct Body {
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

pub(crate) fn lower(ast: &[Item]) -> Mir {
	let mut mir = Mir::default();

	for item in ast {
		match item {
			Item::Proc { name, body } => {
				let body = LowerCtx {
					body: Body::default(),
					scopes: Vec::new(),
					current_reg: Reg(0),
					current_label: Label(0),
					loop_tops: Vec::new(),
					break_fixups: Vec::new(),
				}
				.lower_proc(body);
				mir.bodies.insert(name.clone(), body);
			}
		}
	}

	mir
}

struct LowerCtx {
	body: Body,
	scopes: Vec<HashMap<String, (Reg, Type)>>,
	current_reg: Reg,
	current_label: Label,
	loop_tops: Vec<Label>,
	break_fixups: Vec<Vec<usize>>,
}

impl LowerCtx {
	fn lower_proc(mut self, stmts: &[Stmt]) -> Body {
		self.lower_block(stmts);
		self.body
	}

	fn lower_stmt(&mut self, stmt: &Stmt) {
		match stmt {
			Stmt::LocalDef { name, value, ty } => self.lower_local_def(name, value, ty),
			Stmt::LocalSet { name, new_value } => self.lower_local_set(name, new_value),
			Stmt::Loop { stmts } => self.lower_loop(stmts),
			Stmt::If { condition, true_branch, false_branch } => {
				self.lower_if(condition, true_branch, false_branch)
			}
			Stmt::Break => {
				self.break_fixups.last_mut().unwrap().push(self.body.instrs.len());
				self.emit(Instr::Br { label: Label::PLACEHOLDER })
			}
			Stmt::Continue => self.emit(Instr::Br { label: *self.loop_tops.last().unwrap() }),
		}
	}

	fn lower_local_def(&mut self, name: &str, value: &Expr, ty: &Type) {
		let lower_result = self.lower_expr(value);

		self.expect_types_match(ty, &lower_result.ty);

		// every local gets its own register in case we want to mutate its value later
		let reg = if lower_result.did_allocate_new_reg {
			lower_result.reg
		} else {
			// if lowering the local’s value resulted in just referencing an existing register,
			// we make sure to allocate a new register
			let new_reg = self.next_reg();
			self.emit(Instr::Store { dst: new_reg, src: lower_result.reg });
			new_reg
		};

		self.insert_in_scope(name.to_string(), reg, lower_result.ty);
	}

	fn lower_local_set(&mut self, name: &str, new_value: &Expr) {
		let (local_reg, ty) = self.lookup_in_scope(name);
		let new_value = self.lower_expr(new_value);
		self.expect_types_match(&ty, &new_value.ty);
		self.emit(Instr::Store { dst: local_reg, src: new_value.reg });
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
			match &mut self.body.instrs[idx] {
				Instr::Br { label } => {
					assert_eq!(*label, Label::PLACEHOLDER);
					*label = bottom;
				}
				_ => unreachable!(),
			}
		}
	}

	fn lower_if(&mut self, condition: &Expr, true_branch: &[Stmt], false_branch: &[Stmt]) {
		let condition = self.lower_expr(condition);
		self.expect_types_match(&Type::U64, &condition.ty);
		let br_idx = self.body.instrs.len();
		self.emit(Instr::CondBr { label: Label::PLACEHOLDER, condition: condition.reg });

		self.lower_block(false_branch);
		let skip_br_idx = self.body.instrs.len();
		self.emit(Instr::Br { label: Label::PLACEHOLDER });

		let true_branch_top = self.next_label();
		self.lower_block(true_branch);
		let true_branch_bottom = self.next_label();

		match &mut self.body.instrs[br_idx] {
			Instr::CondBr { label, .. } => {
				assert_eq!(*label, Label::PLACEHOLDER);
				*label = true_branch_top;
			}
			_ => unreachable!(),
		}

		match &mut self.body.instrs[skip_br_idx] {
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
			Expr::Local(name) => {
				let (reg, ty) = self.lookup_in_scope(name);
				LowerExprResult { reg, ty, did_allocate_new_reg: false }
			}
			Expr::Int(n) => {
				let dst = self.next_reg();
				self.emit(Instr::StoreConst { dst, value: Const(*n) });
				LowerExprResult { reg: dst, ty: Type::U64, did_allocate_new_reg: true }
			}
			Expr::Add(lhs, rhs) => {
				let lhs = self.lower_expr(lhs).reg;
				let rhs = self.lower_expr(rhs).reg;
				let dst = self.next_reg();
				self.emit(Instr::Add { dst, lhs, rhs });
				LowerExprResult { reg: dst, ty: Type::U64, did_allocate_new_reg: true }
			}
			Expr::Equal(lhs, rhs) => {
				let lhs = self.lower_expr(lhs).reg;
				let rhs = self.lower_expr(rhs).reg;
				let dst = self.next_reg();
				self.emit(Instr::CmpEq { dst, lhs, rhs });
				LowerExprResult { reg: dst, ty: Type::U64, did_allocate_new_reg: true }
			}
		}
	}

	fn emit(&mut self, instr: Instr) {
		self.body.instrs.push(instr);
	}

	fn insert_in_scope(&mut self, name: String, reg: Reg, ty: Type) {
		self.scopes.last_mut().unwrap().insert(name, (reg, ty));
	}

	fn lookup_in_scope(&mut self, name: &str) -> (Reg, Type) {
		for scope in self.scopes.iter().rev() {
			if let Some((reg, ty)) = scope.get(name) {
				return (*reg, ty.clone());
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
		match self.body.labels.entry(self.body.instrs.len()) {
			Entry::Occupied(e) => *e.get(),
			Entry::Vacant(e) => {
				let l = self.current_label;
				e.insert(l);
				self.current_label.0 += 1;
				l
			}
		}
	}

	fn expect_types_match(&self, ty_1: &Type, ty_2: &Type) {
		assert_eq!(ty_1, ty_2)
	}
}

impl fmt::Debug for Mir {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		writeln!(f, "MIR[[")?;

		for (name, body) in &self.bodies {
			writeln!(f, "    {name}:")?;

			for (i, instr) in body.instrs.iter().enumerate() {
				write!(f, "    ")?;
				match body.labels.get(&i) {
					// this alignment width is larger than you’d expect
					// due to coloring escape codes
					Some(label) => write!(f, "{:>13}:", format!("{label:?}"))?,
					None => write!(f, "     ")?,
				}
				writeln!(f, " {instr:?}")?;
			}

			if let Some(label) = body.labels.get(&body.instrs.len()) {
				writeln!(f, "    {label:?}:")?;
			}
		}

		write!(f, "]]")?;
		Ok(())
	}
}

struct LowerExprResult {
	reg: Reg,
	ty: Type,
	did_allocate_new_reg: bool,
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
		write!(f, "\x1b[33m#{}\x1b[0m", self.0)
	}
}

impl fmt::Debug for Const {
	fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
		write!(f, "\x1b[35m{}\x1b[0m", self.0)
	}
}
