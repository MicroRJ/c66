c66, is a minuscule, work in progress "C" JIT compiler.
- Only supports a tiny subset of C
- Arbitrarily complex expressions are not supported.
- Type definitions are not supported, user types
are not supported. Builtin types are:
	i8,  char
	i16, short
	i32, int
	i64, long long int
- targets x64 Windows.