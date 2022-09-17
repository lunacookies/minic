use logos::Logos;
use text_size::{TextRange, TextSize};

pub(crate) fn lex(text: &str) -> Vec<Token> {
	TokenKind::lexer(text)
		.spanned()
		.map(|(kind, span)| Token {
			kind,
			range: TextRange::new(
				TextSize::from(span.start as u32),
				TextSize::from(span.end as u32),
			),
		})
		.collect()
}

#[derive(Clone, Copy)]
pub(crate) struct Token {
	pub(crate) kind: TokenKind,
	pub(crate) range: TextRange,
}

impl std::fmt::Debug for Token {
	fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
		write!(f, "{:?}@{:?}", self.kind, self.range)
	}
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Logos)]
pub(crate) enum TokenKind {
	#[token("var")]
	Var,

	#[regex("[a-z][a-zA-Z0-9_]*")]
	Ident,

	#[regex("[0-9]+")]
	Int,

	#[token("=")]
	Eq,

	#[token("+")]
	Plus,

	#[error]
	#[regex("[ \t\n]*", logos::skip)]
	Error,
}
