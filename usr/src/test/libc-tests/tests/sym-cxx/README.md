# sym-cxx: C++ symbol visibility tests

This directory contains `sym_cxx_test`, a test runner that verifies C++ symbol
visibility in illumos system headers.  The runner compiles small probe programs
and checks whether they succeed or fail, testing that symbols are (or are not)
visible in `namespace std` and/or the global namespace under various C++
standards.

## How it works

### Overview

1. The runner reads a **compilation config** (`sym-cxx/compilation.cfg`) that
   defines named *environments* (C++ standard + preprocessor flags) and named
   *environment groups* (aliases for sets of environments).
2. It reads one or more **test config** files (e.g. `sym-cxx/math_h.cfg`) that
   describe symbols to test — their type, the header to include, and which
   environments should succeed (`+`) or fail (`-`).
3. For each (test, environment) pair it **generates a small C++ probe program**,
   compiles it, and checks the result against the expectation.

### Config file format

Fields are separated by `|`.  Leading/trailing whitespace in each field is
stripped.  Lines beginning with `#` are comments.

#### `compilation.cfg` — environments

```
env       | NAME        | c++NN  | -DFOO -DBAR
env_group | GROUP_NAME  | NAME1 NAME2 NAME3
```

- `env`: defines one named environment with a C++ standard (`c++11`, `c++14`,
  `c++17`) and optional preprocessor flags.
- `env_group`: defines a shorthand alias for a set of environment names.

Example from `compilation.cfg`:

```
env | CXX11      | c++11 |
env | CXX11_C99  | c++11 | -D_STDC_C99

env_group | CXX11+   | CXX17 CXX14 CXX11
env_group | CXX_C99+ | CXX17_C99 CXX14_C99 CXX11_C99
```

#### Test config files — symbols

The test config files use these directives:

```
type   | TYPE_EXPR          | header.h        | ENVS
value  | name  | type       | header.h        | ENVS
define | NAME  | value      | header.h        | ENVS
func   | name  | rtype | argtypes | header.h  | ENVS
```

The `ENVS` field is a space-separated list of environment names (or group
names) with optional `+` (must compile) or `-` (must NOT compile) prefix.
A bare name without a prefix defaults to `+`.

Multiple headers can be listed in the header field separated by `;`.

### Generated probe programs

For each test entry, the runner generates a minimal C++ source file.  Here are
examples for each directive type.

#### `func` — function call test

Config line:
```
func | std::log | double | double | math.h | CXX11+
```

Generated program:
```cpp
#include <math.h>
double
test_func(double a0)
{
	double result{std::log(a0)};
	return result;
}
```

The brace-initialisation (`result{...}`) is intentional: it prohibits narrowing
conversions, so a missing `float` or `long double` overload that would silently
promote through `double` is caught as a compile error rather than a silent pass.

#### `func` — negative test (symbol must NOT be visible)

Config line:
```
func | log | double | double | math.h | -CXX11+
```

Generated program (same as above, but without `std::`):
```cpp
#include <math.h>
double
test_func(double a0)
{
	double result{log(a0)};
	return result;
}
```

This is compiled with `-Werror`; an ambiguity or implicit declaration causes
failure, which is the *expected* outcome for a `-` entry.

#### `type` — type existence test

Config line:
```
type | std::size_t | stddef.h | CXX11+
```

Generated program:
```cpp
#include <stddef.h>
std::size_t test_type;
```

#### `value` — constant/variable access test

Config line:
```
value | M_PI | double | math.h | CXX11+
```

Generated program:
```cpp
#include <math.h>
double test_value;
void
test_func(void)
{
	test_value = M_PI;
}
```

#### `define` — preprocessor macro test

Config line:
```
define | INFINITY | | math.h | CXX11+
```

Generated program:
```cpp
#include <math.h>
#if !defined(INFINITY)
#error INFINITY is not defined or has the wrong value
#endif
```

An optional second field can specify an expected value:
```
define | FLT_RADIX | 2 | float.h | CXX11+
```

Generated program:
```cpp
#include <float.h>
#if !defined(FLT_RADIX) || FLT_RADIX != 2
#error FLT_RADIX is not defined or has the wrong value
#endif
```

## Running the tests

The test binaries are installed under `/opt/libc-tests/tests/sym-cxx/`.  The
`setup` script (run by the test framework) discovers the available
architectures and runs the appropriate binary for each.

To run a specific config against installed headers:
```sh
/opt/libc-tests/tests/sym-cxx/cmath_h
```

To see compiler output on failures, add `-d` (debug mode):
```sh
/opt/libc-tests/tests/sym-cxx/cmath_h -d
```

### Testing against proto headers

Set `SYM_CXX_ROOT` to redirect the `-isystem` path from `/usr/include` to
a proto area:
```sh
SYM_CXX_ROOT=/path/to/proto/root_i386 \
    /opt/libc-tests/tests/sym-cxx/cmath_h -d
```

The value of `SYM_CXX_ROOT` is always printed unconditionally so test results
are self-documenting.

## Directory layout

```
sym_cxx_test.c          Main test runner (this is the binary built per arch)
setup.ksh               Setup script: detects arches, invokes test runner
README.md               This file

../../../cfg/sym-cxx/
  compilation.cfg       Environment and environment-group definitions
  cmath_h.cfg           Tests for <cmath>
  math_h.cfg            Tests for <math.h>
  math_iso_h.cfg        Tests for <iso/math_iso.h> (C90 declarations)
  math_c99_h.cfg        Tests for C99 functions via <math.h>
```
