# Locklint tests

<details>
<summary>Copyright and license</summary>


```
This file and its contents are supplied under the terms of the
Common Development and Distribution License ("CDDL"), version 1.0.
You may only use this file in accordance with the terms of version
1.0 of the CDDL.

A full copy of the text of the CDDL should have accompanied this
source.  A copy of the CDDL is also available via the Internet at
http://www.illumos.org/license/CDDL.

Copyright 2026 Gordon W. Ross
```

</details>

## Overview

These tests exercise locklint from Sparse parsing through interprocedural
lock analysis.  This directory is intended for developers adding locklint
tests for features and regressions.

Most tests pair a C source file with a `.ref` file containing the exact
expected output.  Other tests check selected properties of larger debug dumps
or commands that are expected to fail.

The suite intentionally includes both valid locking and expected diagnostics.
A successful test run means that locklint emitted exactly the expected
warnings; it does not mean that every fixture is warning-free.

## Running the tests

Build locklint before every test run.  From the repository root, run:

```sh
(cd usr/src/tools/locklint && make all test)
```

The `test` target runs `RunTests.sh` with `tests` as its working directory.
The script expects the locklint executable at `../locklint`; invoke it
directly only when debugging an already rebuilt executable:

```sh
(cd usr/src/tools/locklint/tests && ./RunTests.sh)
```

## Adding a test

Keep each fixture focused on one behavior or closely related group of
behaviors.

Keep automated fixtures stand-alone.  Declare the minimal types, constants,
macros, and function prototypes needed by the test instead of including
installed platform system headers.  Multi-file tests may share declarations
through a header stored in this directory.  Conditional system-header includes
used only by optional manual compatibility tooling are permitted, but the
normal test-suite path must not select or require them.

Add the fixture and its invocation to `RunTests.sh`.  Use the helper matching
the expected result:

- `run_capture` runs a command that must succeed and saves combined standard
  output and standard error.
- `run_failure` runs a command that must fail.
- `compare` compares stable output with a checked-in `.ref` file.
- `compare_no_columns` removes warning column numbers before comparison.
  Use it only when Sparse's rewritten source position makes columns unstable;
  file names, line numbers, and complete messages remain significant.
- `require_match` and `reject_match` check selected properties when a complete
  output comparison would be unnecessarily broad or unstable.

Prefer exact `.ref` comparisons for diagnostics and other compact, stable
output.  Use property checks for large parser or intermediate-representation
dumps.  Do not weaken a comparison merely to accommodate an unexplained
change.

For a multi-file test, pass all translation units to one locklint invocation
in the order needed by the test.  Shared declarations belong in a fixture
header.  Do not use matching pathnames or source positions as a substitute
for testing the intended C identity or linkage behavior.

## Debugging failures

An exact comparison prints a unified diff on failure.  The generated `.out`
file contains the output used for comparison.

Column-insensitive tests first capture the exact diagnostic in a `.raw` file
and write normalized output to `.out`.  They remove `.raw` after a successful
command and comparison, but preserve it on failure so the original column
numbers remain available for diagnosis.

When a test fails:

1. Confirm that locklint itself exited with the expected status.
2. Inspect the unified diff and the corresponding `.out` file.
3. Inspect `.raw` as well when the test uses `compare_no_columns`.
4. Reduce or instrument the fixture if the changed behavior is not clear.
5. Update a `.ref` file only after confirming that every changed diagnostic
   is intentional.

Do not add generated `.out` or `.raw` files to the repository.

## Cleaning generated files

From `usr/src/tools/locklint`, remove the executable, object files, generated
`version.h`, and all test output with:

```sh
make clean
```
