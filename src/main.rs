mod ast;
mod lexer;
mod mir;
mod parser;

use std::{ffi, fs, io};

fn main() -> io::Result<()> {
	for entry in fs::read_dir(".")? {
		let entry = entry?;
		let path = entry.path();
		if path.extension() != Some(ffi::OsStr::new("minic")) {
			continue;
		}

		let content = fs::read_to_string(path)?;
		let tokens = lexer::lex(&content);
		let ast = parser::parse(&tokens, &content);
		dbg!(&ast);
		let mir = mir::lower(&ast);
		dbg!(&mir);
	}

	Ok(())
}
