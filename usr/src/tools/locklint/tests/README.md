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
lock analysis.

Most tests pair a C source file with a `.ref` file containing the exact
expected output.  `RunTests.sh` writes command output to an untracked
`.out` file and compares it with the reference using `diff -u`.  The earliest
smoke tests instead use `grep` to check selected properties of larger debug
dumps.

The suite intentionally includes both valid locking and expected diagnostics.
A successful test run means that locklint emitted exactly the expected
warnings; it does not mean that every fixture is warning-free.

## Running the tests

From `usr/src/tools/locklint`, build locklint and run the complete suite:

```sh
make test
```

After the `locklint` executable has been built, the tests can be rerun
directly:

```sh
(cd tests && ./RunTests.sh)
```

Remove the executable, object files, generated `version.h`, and test output
files with:

```sh
make clean
```

When a golden-output test fails, inspect the unified diff printed by the test
script and the corresponding `.out` file.  Update a `.ref` file only after
confirming that the changed output is intentional.

## Test coverage

These test notes are ordered by the sequence in which the features
were developed.

### `smoke.c`

This is the frontend and source-identity smoke test.  It contains nested
members, arrays, pointers, local objects, global objects, and static objects.

The test driver processes it three ways:

- `--dump-parsed` must produce the parsed function;
- `--dump-linearized` must contain memory loads or stores; and
- `--dump-accesses` must retain selected normalized paths such as
  `arg.nested.value`, `arg.next.value`, and `local.values`.

These checks establish that Sparse can parse and lower the fixture without
losing the source identity needed by later analysis.

### `events.c` and `events.ref`

This test covers ordered source events.  The fixture accesses protected and
unprotected members around `mutex_enter()` and `mutex_exit()`, and includes a
direct function call.

`--dump-events` must match `events.ref`, proving that locklint emits reads,
writes, calls, acquisitions, and releases in control-flow order and marks the
member associated with `MUTEX_PROTECTS_DATA`.

### `annotations.c` and `annotations.ref`

This test covers preprocessing-time `_NOTE` capture and annotation
resolution.  It includes an unsupported annotation that must remain in raw
form and a `MUTEX_PROTECTS_DATA` annotation using a generated member list.

`--dump-annotations` must match `annotations.ref`, proving that raw arguments
survive macro processing and that supported type and member names resolve to
individual protection relations.

### `annotation-names.c` and `annotation-names.ref`

This test covers the complete mutex-protection name forms.  It includes:

- named global locks and data;
- concrete object/member paths;
- nested generated member paths;
- recursive expansion of structure-valued data;
- type-scoped protection applied to embedded structures; and
- overlapping declarations where the last protection declaration wins.

The diagnostic comparison removes only warning column numbers, which vary
with frontend positioning details, while retaining source lines and complete
messages.  A separate annotation dump check verifies that replaced and
effective declarations identify one another.

### `anonymous-embedding.c` and `anonymous-embedding.ref`

This test covers type-scoped protection for members promoted through valid
inline anonymous structures and unions.  It includes direct structure
promotion and nested structure/union promotion.

The diagnostic output proves that promoted data requires the correspondingly
promoted lock and that the retained cumulative offsets identify both members
within the enclosing type.

### `annotation-errors.c`

This test covers recognized annotations whose global, object-path, or
type-member names cannot be resolved.  Locklint must fail and identify every
unresolved name rather than silently ignoring the contract.

### `check.c` and `check.ref`

This is the intraprocedural locking test.  It covers:

- protected access before acquisition and after release;
- protected access while the mutex is held;
- an unrelated member;
- divergent branches that merge to uncertain lock state;
- loops; and
- conditional lock state at function return.

`--check-locks` must match `check.ref`, exercising the `HELD`, `NOT_HELD`, and
`MAYBE_HELD` control-flow states and their diagnostics.

### `calls.c` and `calls.ref`

This test covers entry lock conditions propagated through direct calls.  Its
functions exercise direct and transitive callees, recursion, and conservative
analysis roots.

`--check-locks` must match `calls.ref`, proving that formal-argument
lock conditions map to actual caller objects, a caller-held mutex satisfies
the callee, and unsatisfied conditions are reported at appropriate call sites.

### `global-conditions.c` and `global-conditions.ref`

This test covers direct and transitive lock conditions protected by absolute
locks.  It includes both a bare global mutex and a mutex member within a
global object.

The output proves that absolute lock roots remain distinct from the formal
data argument as lock conditions propagate through calls.

### `effects.c` and `effects.ref`

This test covers mutex side-effect summaries.  It includes functions that
acquire, release, preserve, or conditionally change a formal argument's lock,
as well as transitive and recursive call chains.

`--check-locks` must match `effects.ref`, proving that three-state transfer
tables are solved and applied at call sites and that invalid acquire or release
conditions propagate through direct calls.

### `cross.h`, `cross-caller.c`, `cross-callee.c`, and `cross.ref`

These files cover analysis across translation units.  The shared header
declares the protected structure and external functions; one source file
contains callers and the other contains accessor and acquisition helpers.

Locklint analyzes both C files in one invocation.  The result must match
`cross.ref`, proving that unique external definitions are resolved, protected
member lock conditions and mutex effects cross file boundaries, and member
symbols are remapped through caller argument types.

### External object identity

`external-objects.h`, `external-objects-caller.c`,
`external-objects-callee.c`, and `external-objects.ref` cover object identity
across translation units.  An annotation in one file protects direct accesses
in another, and external bare and member locks satisfy propagated
lock conditions.

Same-named file-static objects in the two files remain distinct.  A later
`extern` declaration retains a visible earlier declaration's internal
linkage, local shadows remain local, and internal and external functions with
the same name resolve within the correct translation unit.

### `forced-include.h`, `forced-include.c`, and `forced-include.ref`

This test passes the header through Sparse's command-line `-include` option.
The header declares external and file-static objects and their protection
annotations before Sparse parses the explicit source input.

The diagnostic proves that initialization-time annotations retain provenance,
resolve while their namespace is current, share the external object's
canonical identity with the later definition, and retain the file-static
object's identity when Sparse reuses its declaration in the input.

The test also supplies two explicit inputs and requires locklint to reject the
invocation.  Sparse parses the forced header only once, so locklint cannot
represent a separate instance of its file-static object for each translation
unit without frontend support.

### `assertions.c` and `assertions.ref`

This test covers `ASSERT` and `VERIFY` as lock-state assumptions.  It includes:

- an `ASSERT` definition that expands to nothing;
- an active definition whose body would call `assfail()`;
- `VERIFY`;
- `MUTEX_HELD` and `MUTEX_NOT_HELD`;
- direct `mutex_owned()` predicates;
- negation; and
- comparison with zero.

The diagnostic output must match `assertions.ref`.  A second event dump checks
that `mutex_owned()` remains visible while `assfail()` does not, proving that
locklint analyzes the assertion condition independently of the configured
macro body.

### `user-mutex.c` and `user-mutex.ref`

This test covers the user-level synchronization interfaces `mutex_lock()` and
`mutex_unlock()`.  It includes a protected access while directly locked, an
unprotected control access, and a helper whose acquisition effect is applied
by its caller.

`--check-locks` must match `user-mutex.ref`, proving that the user-level calls
are decoded as mutex acquisition and release and participate in the same
interprocedural effect analysis as `mutex_enter()` and `mutex_exit()`.
