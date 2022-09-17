use crate::ast::{Expr, LocalDef};
use crate::lexer::{Token, TokenKind};

pub(crate) fn parse(tokens: &[Token], input: &str) -> Vec<LocalDef> {
	Parser { tokens, cursor: 0, input }.parse()
}

struct Parser<'a> {
	tokens: &'a [Token],
	cursor: usize,
	input: &'a str,
}

impl Parser<'_> {
	fn parse(mut self) -> Vec<LocalDef> {
		let mut local_defs = Vec::new();
		while self.cursor < self.tokens.len() {
			local_defs.push(self.local_def());
		}
		local_defs
	}

	fn local_def(&mut self) -> LocalDef {
		self.expect(TokenKind::Var);
		let name = self.expect(TokenKind::Ident);
		self.expect(TokenKind::Eq);
		let value = self.expr();
		LocalDef { name, value }
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

		self.error(&format!("{actual_kind:?}"))
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
