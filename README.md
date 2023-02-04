###### minic

a tiny c-ish language, hopefully.

###### philosophy

- predictability
- simplicity
- familiarity
- don’t put performance above everything else
- minimize undefined behavior
- optimize for tooling
- lightning fast compilation by replacing LLVM
  with something less aggressive
- generics, if they end up in the language,
  should be very limited
  to avoid [the problem with typeclasses](https://github.com/fsharp/fslang-suggestions/issues/243#issuecomment-916079347)
- reduce the need for a borrow checker
  with runtime memory safety checks
  and a culture of simple ownership semantics
  (pervasive use of arenas)

###### implementation notes

- written in C11 for now
- compiles to aarch64 asm
- currently only works on macOS running on Apple Silicon
- allocates all memory at startup –
  no dynamic memory allocation whatsoever
- resilient to errors in source code
- architected for multithreaded builds,
  though that isn’t necessary at the moment
  since there exist no large codebases
  for which multithreading would be beneficial
- _not_ architected for IDEs;
  that comes later when I implement the ...
- self hosted compiler! (one day)
