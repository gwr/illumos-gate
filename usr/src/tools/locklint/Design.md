# Locklint design

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

## Purpose

Locklint is a standalone static analyzer for illumos locking contracts.  It
uses Sparse as its C frontend.  Sparse also provides the foundation for
smatch, but smatch and locklint are separate analysis programs; neither is
built on the other.  In the illumos source tree, the maintained Sparse
sources reside under `usr/src/tools/smatch/src`.  Locklint compiles the Sparse
source modules it needs directly and does not use smatch's checker code.

The long-term goal for locklint is to provide illumos with locking analysis
equivalent to historical Solaris `lock_lint`.  Its annotations and behavior
define the compatibility target, with the historical tools used for
comparison.  Lock-specific behavior remains in locklint where practical so
changes to the shared Sparse frontend stay minimal.

This document describes the implemented design.  It is intended to evolve
with the code and eventually provide:

- an operational overview;
- the important data structures and their invariants;
- the sequences used to capture, resolve, and analyze locking information;
- descriptions of the important functions; and
- the boundary between locklint and the Sparse library.

The development roadmap, compatibility goals, and feature priorities are
maintained separately.  This document records how implemented features work,
not when future features should be added.

## Terminology

- **Annotation** - One captured `_NOTE(...)` invocation, including its body
  and source position.  A recognized annotation such as
  `MUTEX_PROTECTS_DATA` is resolved into a protection relation; an unsupported
  `_NOTE` body remains recorded but has no semantic effect.
- **Annotation reference** (`struct annotation_ref`) - One named lock or data
  endpoint within a recognized annotation.  It begins as a parsed name and
  becomes associated with resolved symbols, types, members, and offsets.
  Expanding an aggregate data name may generate additional annotation
  references that still belong to the original annotation.

  For example:

  ```c
  _NOTE(MUTEX_PROTECTS_DATA(record.lock, record.field))
  ```

  Here, `record` is an object in the program.  The annotation contains one
  lock reference for `record.lock` and one data reference for `record.field`.
- **Assertion** - An `ASSERT(...)` or `VERIFY(...)` invocation that provides
  local lock-state facts rather than a persistent protection rule.
- **Protection relation** - An association between protected data and the
  lock required when accessing it.
- **Lock state** - Locklint's estimate that a lock is held, not held, or
  possibly held at a particular program point.
- **Function summary** - Information computed about a function for use by its
  callers.
- **Lock effect** - The part of a function summary that describes how the
  function changes visible lock state.
- **Requirement** - The part of a function summary that describes a lock
  callers must hold because the function accesses protected data.
- **Analysis root** - A function treated as externally reachable or as not
  safely attributable to a known caller.  Roots seed call-graph reachability
  and are boundaries at which unsatisfied requirements cannot simply be
  deferred to a known caller.
- **Flow analysis** - Analysis that follows possible execution paths through
  the program, including control flow within functions and calls between
  functions.

## Operational overview

Locklint reads and parses all specified C source files into an intermediate
representation.  It records locking annotations and assertions, connects
calls among the parsed functions, and analyzes possible execution paths to
determine lock state, function requirements, and lock effects.  After
analysis is complete, it reports accesses that violate declared locking
protections.

Locklint performs flow analysis over all retained functions reachable from
its analysis roots.  This analysis determines lock state and propagates
function summaries through known direct calls.

Sparse parses C source and produces the intermediate representation used by
locklint.  Locklint registers callbacks to capture locking information during
preprocessing, then examines Sparse's symbols, expressions, instructions, and
control-flow graphs to perform its analysis.

The main phases are:

1. Parse locklint command-line options and leave ordinary compiler options for
   Sparse.
2. Register preprocessing hooks for locking annotations and assertions.
3. Initialize Sparse and process any symbols produced during initialization.
4. For each input file:
   1. parse and evaluate the translation unit;
   2. resolve annotations while that translation unit's symbol namespaces are
      current;
   3. expand and linearize function definitions; and
   4. retain each function entrypoint for later checking.
5. After all files have been parsed:
   1. identify direct callers and conservative analysis roots;
   2. solve function lock-effect summaries;
   3. solve intraprocedural lock state;
   4. collect and propagate protected-access requirements; and
   5. emit diagnostics using the stable summaries.
6. Emit requested annotation or development dumps.

Locklint deliberately delays diagnostics until summaries stabilize.  A
warning seen during an early pass could otherwise be invalidated by a later
callee summary.

## Data model overview

Locklint's data model has two layers.  Sparse owns the parsed program and its
intermediate representation.  Locklint retains references to that
representation and adds records for locking declarations, normalized object
identities, per-function state, and function summaries.

### Sparse intermediate representation

The principal Sparse structures used by locklint are:

| Structure | Role in locklint |
| --- | --- |
| `struct symbol` | Identifies declarations, types, functions, objects, and structure members |
| `struct expression` | Retains source-level expressions and member identity needed to recover the object named by an access |
| `struct instruction` | Represents a load, store, call, or other operation examined by locklint |
| `struct basic_block` | Groups instructions along one portion of a function's control flow and links predecessor and successor blocks |
| `struct entrypoint` | Represents one linearized function and provides its basic blocks and function symbol |

These objects form the program representation over which locklint performs
flow analysis.  Locklint refers to them directly rather than copying the
entire representation.

### Locklint records

The main locklint-owned records relate as follows:

```text
annotations
  `- annotation
       |- annotation_token list
       |- lock annotation_ref
       `- data annotation_ref list

assertions
  `- assertion

functions
  `- function_info
       |- borrowed Sparse entrypoint
       |- block_info list
       |    |- input state_entry list
       |    `- output state_entry list
       |- requirement list
       `- lock_transfer list
```

The structures have these roles:

| Structure | Role and important relationships |
| --- | --- |
| `struct annotation` | One captured `_NOTE(...)`; owns copied tokens and refers to one lock reference and a list of data references |
| `struct annotation_token` | A durable copy of one preprocessing token from an annotation body |
| `struct annotation_ref` | One parsed and later resolved lock or data endpoint; refers to Sparse symbols and records replacement precedence |
| `struct assertion` | One recognized lock predicate captured from `ASSERT(...)` or `VERIFY(...)`; records source ranges and the asserted state |
| `struct locklint_access` | A normalized identity for an object or member access; refers to its root symbol, type, final member, cumulative offset, and source expression |
| `struct function_info` | Locklint's record for one function; refers to its Sparse entrypoint and owns its block state and summaries |
| `struct block_info` | Associates one Sparse basic block with reachability and its input and output lock-state maps |
| `struct state_entry` | One lock and its state in a block-state map; embeds a `locklint_access` identity |
| `struct requirement` | A caller-visible protected-access requirement associated with a formal data argument and either a relative lock or a preserved absolute lock root |
| `struct lock_transfer` | A function lock-effect summary for one formal lock and each possible input state |
| `struct transfer_block_info` | Temporary per-block state used while computing one lock transfer |

The process-wide annotation, assertion, and function lists connect data
captured from all input files.  References to Sparse symbols and expressions
tie those records back to the parsed program.

### Ownership and lifetime

- Sparse owns symbols, expressions, entrypoints, blocks, and instructions.
- Locklint borrows those objects for the duration of the process.
- Locklint copies `_NOTE` text and positions because Sparse frees per-file
  preprocessing tokens.
- Locklint allocates annotations, references, assertions, and copied `_NOTE`
  tokens for process lifetime.
- Function records, requirements, transfers, CFG state maps, and
  transfer-simulation blocks are freed after their analysis use ends.
- The implementation is a batch command, so process-lifetime metadata is
  intentional.  A future library or daemon interface would require explicit
  teardown and per-invocation ownership.

## Sparse integration

This section describes the Sparse interfaces and retained metadata on which
locklint depends.  The integration boundary consists of the following entry
points, hooks, and retained source metadata.

### Frontend entry points

`sparse_initialize()` initializes Sparse, consumes Sparse/compiler options,
collects input file names, creates predefined declarations, and returns any
initial symbols.  Locklint processes those symbols before processing the
explicit file list.

`sparse(file)` creates a new file scope, parses and evaluates one translation
unit, and returns its symbol list.  Sparse token storage for the file is
released during this operation.  Therefore, data needed after parsing must
not retain pointers to ordinary preprocessing tokens.

`expand_symbol()` completes Sparse expansion for a symbol.
`linearize_symbol()` converts a function definition into an `entrypoint`
containing basic blocks and instructions.  Locklint retains these entrypoints
until interprocedural checking completes.

### Macro expansion hooks

Locklint adds a small, general hook facility to Sparse preprocessing:

```c
add_macro_expansion_hook(name, callback, data)
```

Sparse calls matching hooks after recognizing a function-like macro
invocation and its opening parenthesis, but before collecting or expanding
the arguments.  The callback receives the macro token and opening-parenthesis
token.

Hook callbacks have two modes:

- returning zero observes the original invocation but leaves normal macro
  substitution unchanged;
- returning nonzero requests that Sparse substitute and analyze the expanded
  first argument instead of the configured macro body.

Multiple hooks may be registered.  Sparse calls every hook whose name
matches, and preserves the first argument if any matching callback requests
it.

Locklint registers these hooks:

| Macro | Callback behavior |
| --- | --- |
| `_NOTE` | Copies the original argument tokens and returns zero.  The normal `_NOTE` macro expansion still occurs. |
| `ASSERT` | Records recognized mutex predicates and returns nonzero.  Sparse analyzes the assertion condition rather than an empty or implementation-specific macro body. |
| `VERIFY` | Uses the same behavior as `ASSERT`. |

`_NOTE` token copies use ordinary heap allocation because Sparse releases the
translation unit's token arena after parsing.  The copies and other global
analysis records currently live until process exit.

### Retained expression identity

Normal Sparse evaluation rewrites member expressions into lower-level address
and offset forms.  That loses the source object/member path needed to match a
memory access with a locking annotation.

The maintained Sparse expression structure therefore retains:

| Field | Meaning |
| --- | --- |
| `member_base` | Source expression below the current member selection |
| `member_ident` | Identifier selected at this path component |
| `member_symbol` | Sparse member symbol resolved for that identifier |
| `member_path_offset` | Offset resolved for this selection, including promotion through anonymous aggregates |

Sparse sets these fields while evaluating a member dereference and preserves
them when evaluation rewrites address-of expressions or degenerates arrays.

The retained chain is source metadata; Sparse continues using its normal
evaluated expression for semantic analysis and lowering.

### Retained instruction expressions

Sparse linearization retains source expressions on the instructions consumed
by locklint:

| Instruction | Retained field |
| --- | --- |
| `OP_LOAD` | `instruction.access` |
| `OP_STORE` | `instruction.access` |
| `OP_CALL` | `instruction.call_expr` |

The load/store expression permits reconstruction of the source object and
member path.  The call expression permits inspection of source arguments and
mapping from callee formals to caller actuals.

These fields are borrowed pointers into Sparse expression storage.  Locklint
does not free or replace them and assumes they remain valid through the
whole-invocation analysis.

## Source object and member identity

### `struct locklint_access`

`locklint_access` is the common identity used for memory accesses and locks:

| Field | Meaning |
| --- | --- |
| `root` | Sparse symbol at the root of the source expression |
| `type` | Compound type reached from the root after stripping pointer and node wrappers |
| `member` | Final resolved member symbol, or `NULL` for a whole object |
| `offset` | Cumulative byte offset of the selected path from the root |
| `expr` | Retained source expression, when path traversal is still required |

The current lock-state equality relation is:

```text
same root symbol + same final member symbol + same cumulative offset
```

The final member distinguishes fields, while the cumulative offset
distinguishes repeated or nested occurrences of a member symbol.  The root
distinguishes separate objects.

`locklint_get_access()` recovers this identity from a retained expression.
`find_root()` searches through the expression operators supported by the
current model.  `find_member()` locates the retained member chain.
`member_offset()` sums `member_path_offset` across that chain.

`locklint_access_base()` attempts to determine where a type-scoped annotation
owner occurs within a concrete access.  It walks from the final member toward
the root, accumulating suffix offsets.  A match supplies the base offset of
the annotated object within the concrete root.  Valid inline anonymous
structures and unions do not introduce a separately named owner type;
Sparse's retained path offset includes their promotion offsets, so an
annotation on the enclosing type uses the normal exact offset match.

### Identity limitations

The identity model is deliberately smaller than an alias analysis.  It does
not currently unify assigned aliases, container conversions, or multiple
formal arguments that name one object.

Two known cases require additional work:

1. Externally linked object symbols parsed in separate translation units are
   not yet canonicalized, so pointer identity is too strict for those
   objects.
2. Arrays use an offset-based identity but do not yet have a documented,
   tested policy for distinguishing all dynamic element expressions.

These are correctness boundaries, not merely diagnostic limitations.  A
failed identity match can cause a protected access to be missed.

## Annotation representation

### Capture

Each `_NOTE` invocation becomes an `annotation` containing:

- the source position of the invocation;
- copied raw tokens;
- one parsed lock reference;
- a list of parsed data references; and
- parse, processing, and resolution state.

Unsupported annotation names retain their raw tokens for dumps but do not
currently affect checking.

### Names and references

An `annotation_ref` represents one symbolic lock or data name.  Its important
fields are:

| Field | Meaning |
| --- | --- |
| `scope` | Initially automatic, explicitly type-scoped, or explicitly object-scoped |
| `base_name` | First identifier in the annotation name |
| `path` | Remaining dot-separated path, if any |
| `root` | Concrete object symbol for object-scoped names |
| `owner_type` | Compound type in which a type-scoped path is interpreted |
| `type` | Type reached after resolving the path |
| `member` | Final member symbol |
| `offset` | Offset of the path within its root or owner type |
| `replaces`, `replaced_by` | Links recording declaration precedence |

Automatic names first resolve in the ordinary object namespace, then as a
structure or typedef name.  A successful object lookup changes the reference
to object scope; a successful type lookup changes it to type scope.

The parser accepts:

- bare object or type names;
- concrete paths such as `object.member`;
- type paths such as `type::member`;
- generated member lists such as `type::{ first second }`; and
- nested generators such as `type::{ nested.{ first second } }`.

Only `MUTEX_PROTECTS_DATA` is converted into semantic relations today.

### Resolution and expansion

`locklint_resolve_annotations()` processes only annotations not handled by an
earlier translation-unit pass.  Resolution occurs before the driver advances
to the next file so the current type and object namespaces remain available.

Resolution performs these steps:

1. Resolve the lock name.
2. Resolve every data name.
3. Recursively expand structure-valued data into leaf members.
4. Skip the protecting lock member when recursively expanding the same
   object/type.
5. Record overlaps with earlier resolved data references.
6. Mark the annotation resolved only if every required name succeeded.

Anonymous aggregate carriers are transparent during recursive expansion.
Named leaf members inherit the accumulated offset.

### Replacement semantics

For a selected protected datum, the last resolved declaration in source
processing order wins.  `record_replacements()` links the earlier and later
references for annotation dumps.  `locklint_protecting_access()` scans
relations in order and retains the last matching protector.

This replacement relation currently covers only the implemented mutex
protection mechanism.  Future policy dimensions such as unlocked reads or
read-only data must not be represented as replacements for write protection.

### Matching an access

`locklint_protecting_access()` matches:

- concrete object annotations by root symbol, final member, and root-relative
  offset; or
- type-scoped annotations by final member plus a successful
  `locklint_access_base()` owner/offset match.

It then constructs the concrete protecting lock identity.  A concrete lock
keeps its own root and offset.  A type-scoped lock is based at the matched
instance of the annotated data owner.

External object identity across translation units is a known missing case.

## Assertion representation

The assertion hook scans the unexpanded first argument of `ASSERT` and
`VERIFY` for recognized mutex predicates.  Each recognized predicate becomes
an `assertion` with:

- macro invocation position;
- predicate position;
- predicate argument range; and
- implied held or not-held state.

Recognized predicates currently include `MUTEX_HELD`, `MUTEX_NOT_HELD`,
`mutex_owned`, and `_mutex_held`.  A directly preceding `!` and direct
comparison with zero adjust the implied state.

After linearization, `locklint_get_assertion()` associates an `OP_CALL`
predicate with captured source positions and recovers its lock argument.
Conflicting predicates whose individual source locations cannot be
distinguished are rejected instead of guessed.

Assertions refine local state.  They do not acquire or release a lock and are
not included in function effect summaries.  An asserted held state is marked
as not being a function side effect so it does not produce a held-on-return
warning by itself.

## Event decoding

`events.c` provides a shared interpretation of relevant Sparse instructions.
It recognizes:

- reads from `OP_LOAD`;
- writes from `OP_STORE`;
- calls from `OP_CALL`;
- acquisitions by `mutex_enter()` and `mutex_lock()`; and
- releases by `mutex_exit()` and `mutex_unlock()`.

`locklint_get_lock_action()` returns the action and normalized first argument
for a recognized mutex operation.  Both the checker and `--dump-events` use
this decoder so development output and semantic checking agree.

The current user-level `mutex_lock()` model assumes successful acquisition.
Its return value and robust-mutex states are not modeled.

## Intraprocedural lock state

### State domain

Each tracked lock has one of three states:

| State | Meaning |
| --- | --- |
| `LOCK_NOT_HELD` | Definitely not held; represented by no map entry |
| `LOCK_HELD` | Definitely held |
| `LOCK_MAYBE_HELD` | Held on some incoming paths and not held on others |

A `state_entry` stores the normalized lock identity, its state, and a
`side_effect` flag.  The flag distinguishes state caused by a lock operation
from state introduced only by an assertion.

### Block analysis

Each `block_info` associates a Sparse basic block with:

- reachability;
- the state entering the block; and
- the state leaving the block.

`analyze_blocks()` repeatedly:

1. merges reachable predecessor outputs;
2. applies instructions in block order; and
3. compares new input/output maps with the previous iteration.

At a merge, equal states remain unchanged and disagreements become
`LOCK_MAYBE_HELD`.  Iteration continues until no block changes.

Instruction transfer order is:

1. apply summarized direct-call effects;
2. apply a direct mutex acquire or release; and
3. apply an assertion refinement.

Only `LOCK_HELD` satisfies a protected access.

## Direct-call resolution and roots

For a call whose Sparse symbol already has an entrypoint, locklint uses that
definition directly.

For an unresolved non-static call, `find_external_function()` compares the
identifier text against all retained non-static definitions.  Exactly one
match is accepted.  Multiple matches are treated as ambiguous and diagnosed
at the call.

This identifier-based remapping is necessary because separate translation
units have separate Sparse symbol identities.

Root classification is currently conservative:

- functions with no known direct caller are roots;
- non-static functions are roots; and
- address-taken static functions are not eligible for deferred local
  requirements.

Reachability from those roots is propagated through known direct calls.
Static functions reached only by known direct calls may defer protected-data
warnings to callers.

Unresolved indirect calls are not yet connected to possible targets.

## Function requirements

A `requirement` summarizes a protected access that a caller may need to
satisfy.  It currently records:

- the formal argument containing the protected object;
- an absolute lock root, or `NULL` when the lock is relative to that argument;
- the lock member and offset;
- the protected data member; and
- a representative source position.

`collect_local_requirements()` replays each reachable block from its stable
input state.  Every unsatisfied protected access rooted at a formal argument
becomes a requirement.  A protecting lock rooted at the protected object is
recorded as argument-relative; any other root is preserved as an absolute
lock.  Deferral eligibility is applied later, while emitting diagnostics, to
decide whether a caller-satisfiable requirement suppresses the local warning.

`map_call_requirement()` maps the protected-data argument and protecting lock
separately.  It rebases a relative lock onto the caller's actual argument, but
uses a preserved absolute root unchanged.  The mapped data object determines
whether a requirement can propagate through another formal argument and
whether its local diagnostic can be deferred.

`propagate_function_requirements()` repeatedly maps callee formal arguments
to caller actual arguments and adds unsatisfied requirements to the caller.
The pass iterates to a fixed point, including recursive call cycles.

## Function lock effects

A `lock_transfer` describes the behavior of one formal lock.  It records:

- the formal argument index;
- the lock member and offset;
- an output state for each possible input state; and
- invalid-acquire and invalid-release flags for each input state.

The three input states form a transfer table rather than a single effect
label.  This permits the same representation to describe acquisition,
release, balanced operations, and conditional effects.

Effect construction has three stages:

1. `collect_local_transfers()` finds formal locks used by direct mutex
   operations.
2. `propagate_transfer_candidates()` adds formal locks affected through
   callees.
3. `solve_function_transfers()` simulates each candidate for every input
   state.

`simulate_transfer()` performs a CFG fixed point for one candidate/input
combination.  It combines output states and possible invalid operations from
all return paths.

The candidate and table passes repeat together until no summary changes.
This supports transitive effects and recursive cycles without requiring
callees to be processed in a particular order.

At a call site, the formal lock member is resolved through the actual
argument's type.  This remaps member symbols when caller and callee came from
different translation units.

## Diagnostic sequence

After summaries and block states stabilize, `emit_diagnostics()` replays each
reachable block in instruction order.

For each instruction it:

1. checks protected memory access against current state;
2. checks direct-call requirements and invalid summarized effects;
3. checks direct acquire/release validity and applies the operation; and
4. applies assertion refinement.

After the block, a return instruction triggers checks for locks held on all or
some return paths.  Assertion-only held state is excluded from those
side-effect diagnostics.

Diagnostics currently have human-readable text but no stable identifier,
machine-readable format, or predecessor/call-chain witness.

## Important functions by subsystem

### Driver: `locklint.c`

| Function | Responsibility |
| --- | --- |
| `options()` | Consume locklint-specific options while preserving Sparse options |
| `process_symbols()` | Expand symbols, create entrypoints, register checking, and emit requested dumps |
| `main()` | Register hooks, drive per-file parsing/resolution, and start whole-invocation analysis |
| `locklint_init_include_path()` | Replace Sparse's default include-path initialization |

### Access identity: `access.c`

| Function | Responsibility |
| --- | --- |
| `find_root()` | Recover the root symbol from supported expression forms |
| `find_member()` | Find retained member metadata in an expression |
| `locklint_get_access()` | Construct normalized root/member/offset identity |
| `locklint_access_base()` | Map a type-scoped relation into a concrete embedded object |
| `locklint_show_access()` | Display a source-oriented access path |

### Annotations: `annotations.c`

| Function | Responsibility |
| --- | --- |
| `capture_annotation()` | Copy `_NOTE` argument tokens before expansion/token release |
| `parse_annotation()` | Parse supported `MUTEX_PROTECTS_DATA` syntax |
| `resolve_annotation_ref()` | Resolve object/type scope and a symbolic path |
| `expand_compound_ref()` | Recursively expand structure-valued data |
| `record_replacements()` | Record last-declaration-wins provenance |
| `locklint_resolve_annotations()` | Process newly captured annotations in the current translation unit |
| `locklint_protecting_access()` | Find the effective protection relation and construct its concrete lock |

### Assertions: `assertions.c`

| Function | Responsibility |
| --- | --- |
| `capture_assertion()` | Record recognized predicates and request first-argument preservation |
| `adjust_state()` | Apply direct negation and zero-comparison semantics |
| `locklint_get_assertion()` | Match a linearized predicate call to captured source metadata |

### Events: `events.c`

| Function | Responsibility |
| --- | --- |
| `call_name()` | Return a direct call identifier or `<indirect>` |
| `locklint_get_lock_action()` | Decode supported mutex acquire/release calls |
| `locklint_show_events()` | Display relevant instructions in CFG order |

### Checker: `check.c`

| Function | Responsibility |
| --- | --- |
| `analyze_blocks()` | Solve intraprocedural lock-state maps |
| `direct_callee()` | Resolve a direct call within or across translation units |
| `mark_direct_callers()` | Classify roots and direct-call reachability |
| `collect_local_transfers()` | Discover formal lock-effect candidates |
| `propagate_transfer_candidates()` | Carry effect candidates through calls |
| `simulate_transfer()` | Compute one transfer-table entry |
| `collect_local_requirements()` | Summarize deferred protected accesses |
| `map_call_requirement()` | Map a requirement's data object and relative or absolute lock at a call |
| `propagate_function_requirements()` | Carry requirements through calls to a fixed point |
| `emit_diagnostics()` | Replay stable state and emit warnings |
| `locklint_check_all()` | Order and iterate the complete analysis |

## Main action sequences

### `_NOTE` to protected-access diagnostic

```text
preprocessor sees _NOTE
    -> capture_annotation copies original tokens
translation unit parsing completes
    -> locklint_resolve_annotations parses and resolves names
    -> structure-valued data expands to leaf relations
function is evaluated and linearized
    -> OP_LOAD/OP_STORE retains source expression
checker visits instruction
    -> locklint_get_access constructs concrete identity
    -> locklint_protecting_access selects effective relation
    -> current lock state is queried
    -> warning is emitted unless the lock is definitely held
```

### Assertion to local state refinement

```text
preprocessor sees ASSERT or VERIFY
    -> capture_assertion records source ranges and implied state
    -> hook requests analysis of the first macro argument
Sparse linearizes recognized predicate call
    -> OP_CALL retains source call expression
checker visits instruction
    -> locklint_get_assertion matches source position/range
    -> predicate argument becomes a locklint_access
    -> block state is refined without recording a lock side effect
```

### Direct callee effect

```text
callee formal lock candidate discovered
    -> transfer table solved for NOT_HELD, HELD, MAYBE_HELD
caller OP_CALL resolved to callee
    -> callee formal argument mapped to caller actual expression
    -> member symbol remapped through actual argument type
    -> current caller state selects transfer-table entry
    -> invalid operation flags are diagnosed
    -> output state replaces caller state
```

### Protected-access requirement

```text
callee protected access is unsatisfied
    -> formal data access becomes a requirement
    -> relative lock or absolute lock root is recorded
caller OP_CALL resolved to callee
    -> formal data object mapped to actual argument
    -> relative lock rebased to actual object
       or absolute lock root preserved unchanged
    -> held caller state satisfies requirement
    -> otherwise warning is emitted or requirement is deferred again
```

## Invariants

The current implementation relies on these invariants:

1. All macro hooks are registered before Sparse starts preprocessing input.
2. Raw annotation tokens needed after parsing are copied.
3. Annotation names are resolved while their defining translation unit's
   namespaces are current.
4. Every checked load, store, and call retains its source expression.
5. A lock-state key is stable for the lifetime of the analysis.
6. Absent lock-state entries mean definitely not held.
7. Only definitely held state satisfies a protection requirement.
8. Requirement propagation maps the protected-data argument independently
   and never rebases an absolute lock root.
9. Assertions refine state but do not create effect summaries.
10. Interprocedural effects and requirements reach fixed points before
   diagnostics are emitted.
11. Multiple external function definitions with one identifier are ambiguous,
    not arbitrarily selected.
12. A later protection declaration replaces an earlier relation for the same
    resolved datum.

Changes that invalidate one of these invariants should update this document
and add a focused regression test.

## Current design limits and follow-up work

The implemented design remains intentionally narrow.  Important missing
areas include:

- canonical identity for external objects across translation units;
- general alias and nested-object identity;
- indirect-call and callback target resolution;
- module scope and auditable root configuration;
- rwlock read/write state;
- visibility and competing-thread policy;
- data-policy annotations other than `MUTEX_PROTECTS_DATA`;
- explicit side-effect contracts;
- condition waits, try-locks, upgrades, and downgrades;
- lock-order analysis; and
- stable diagnostic identifiers, suppressions, and provenance.

These limitations should remain visible here as the implementation evolves.
When a limitation is removed, its replacement design, invariants, and action
sequence should be documented in the same change.
