mod lexer;

use std::{ffi, fs, io};

fn main() -> io::Result<()> {
	for entry in fs::read_dir(".")? {
		let entry = entry?;
		let path = entry.path();
		if path.extension() != Some(ffi::OsStr::new("minic")) {
			continue;
		}

		let content = fs::read_to_string(path)?;
		dbg!(lexer::lex(&content));
	}

	Ok(())
}
