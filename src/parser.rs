use crate::ast::{Expr, Item, Stmt};
use crate::lexer::{Token, TokenKind};

pub(crate) fn parse(tokens: &[Token], input: &str) -> Vec<Item> {
	Parser { tokens, cursor: 0, input }.parse()
}

struct Parser<'a> {
	tokens: &'a [Token],
	cursor: usize,
	input: &'a str,
}

impl Parser<'_> {
	fn parse(mut self) -> Vec<Item> {
		let mut items = Vec::new();
		while self.cursor < self.tokens.len() {
			items.push(self.item());
		}
		items
	}

	fn item(&mut self) -> Item {
		match self.current().kind {
			TokenKind::Proc => self.proc(),
			_ => self.error("item"),
		}
	}

	fn proc(&mut self) -> Item {
		self.expect(TokenKind::Proc);
		let name = self.expect(TokenKind::Ident);
		self.expect(TokenKind::LParen);
		self.expect(TokenKind::RParen);
		let body = self.block();
		Item::Proc { name, body }
	}

	fn stmt(&mut self) -> Stmt {
		match self.current().kind {
			TokenKind::Var => self.local_def(),
			TokenKind::Set => self.local_set(),
			TokenKind::Loop => self.loop_(),
			TokenKind::If => self.if_(),
			TokenKind::Break => self.break_(),
			TokenKind::Continue => self.continue_(),
			_ => self.error("statement"),
		}
	}

	fn local_def(&mut self) -> Stmt {
		self.expect(TokenKind::Var);
		let name = self.expect(TokenKind::Ident);
		self.expect(TokenKind::Eq);
		let value = self.expr();
		Stmt::LocalDef { name, value }
	}

	fn local_set(&mut self) -> Stmt {
		self.expect(TokenKind::Set);
		let name = self.expect(TokenKind::Ident);
		self.expect(TokenKind::Eq);
		let new_value = self.expr();
		Stmt::LocalSet { name, new_value }
	}

	fn loop_(&mut self) -> Stmt {
		self.expect(TokenKind::Loop);
		let stmts = self.block();
		Stmt::Loop { stmts }
	}

	fn if_(&mut self) -> Stmt {
		self.expect(TokenKind::If);
		let condition = self.expr();
		let true_branch = self.block();
		self.expect(TokenKind::Else);
		let false_branch = self.block();
		Stmt::If { condition, true_branch, false_branch }
	}

	fn break_(&mut self) -> Stmt {
		self.expect(TokenKind::Break);
		Stmt::Break
	}

	fn continue_(&mut self) -> Stmt {
		self.expect(TokenKind::Continue);
		Stmt::Continue
	}

	fn block(&mut self) -> Vec<Stmt> {
		let mut stmts = Vec::new();
		self.expect(TokenKind::LBrace);
		while self.current().kind != TokenKind::RBrace {
			stmts.push(self.stmt());
		}
		self.expect(TokenKind::RBrace);
		stmts
	}

	fn expr(&mut self) -> Expr {
		match self.current().kind {
			TokenKind::Ident => {
				let name = self.expect(TokenKind::Ident);
				Expr::Local(name)
			}
			TokenKind::Int => {
				let int = self.expect(TokenKind::Int);
				Expr::Int(int.parse().unwrap())
			}
			TokenKind::Plus => {
				self.expect(TokenKind::Plus);
				let lhs = self.expr();
				let rhs = self.expr();
				Expr::Add(Box::new(lhs), Box::new(rhs))
			}
			TokenKind::Eq => {
				self.expect(TokenKind::Eq);
				let lhs = self.expr();
				let rhs = self.expr();
				Expr::Equal(Box::new(lhs), Box::new(rhs))
			}
			_ => self.error("expression"),
		}
	}

	fn expect(&mut self, kind: TokenKind) -> String {
		let actual_kind = self.current().kind;
		let range = self.current().range;

		if actual_kind == kind {
			self.cursor += 1;
			return self.input[range].to_string();
		}

		self.error(&format!("{kind:?}"))
	}

	fn error(&self, message: &str) -> ! {
		let range = self.current().range;
		let start = u32::from(range.start()) as usize;
		let end = u32::from(range.end()) as usize;
		eprintln!(
			"syntax error: expected {}, got {:?} at {:?}\n{}",
			message,
			self.current().kind,
			range,
			&self.input[start.saturating_sub(20)..(end + 20).min(self.input.len())]
		);
		std::process::exit(1)
	}

	fn current(&self) -> Token {
		self.tokens[self.cursor]
	}
}
