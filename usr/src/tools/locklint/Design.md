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
changes to the shared Sparse frontend stay minimal and generic.  Sparse
changes may provide reusable interfaces or preserve general source metadata,
but do not encode locklint annotations, locking concepts, or locklint policy.

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
- **Lock condition** - A required or asserted lock state at a particular
  program point.  A function entry lock condition is part of a function
  summary and must be established by the caller.
- **Analysis root** - A function treated as externally reachable or as not
  safely attributable to a known caller.  Roots seed call-graph reachability
  and are boundaries at which unsatisfied lock conditions cannot simply be
  deferred to a known caller.
- **Root reason** - One independently established reason for treating a
  function as an analysis root.  A function may have several reasons, each
  with its own provenance.
- **Function-pointer evidence** - A source or intermediate-representation
  observation that a function is used as a value, or that a function pointer
  is initialized, loaded, stored, or copied.  Evidence supports conservative
  root classification and call-graph auditing; it does not by itself prove an
  indirect-call target.
- **Flow analysis** - Analysis that follows possible execution paths through
  the program, including control flow within functions and calls between
  functions.
- **Source file** - A physical file containing source text.  A header can be
  read while parsing more than one translation unit, so a source pathname
  alone does not identify one semantic declaration.
- **Translation unit** - One top-level input file together with the headers
  and macro expansions processed for that input.  Locklint treats each input
  occurrence as a distinct translation unit even when two inputs read some of
  the same physical source files.  Locklint also creates a synthetic
  translation-unit record for declarations Sparse processes during
  initialization, including command-line-included source.
- **Object identity** - Locklint's canonical key for determining when two
  declarations or accesses name the same C object.
- **Source origin** - The translation unit and physical source position from
  which a locklint record was derived.

## Operational overview

Locklint reads and parses all specified C source files into an intermediate
representation.  It records locking annotations and assertions, connects
calls among the parsed functions, and analyzes possible execution paths to
determine lock state, function lock conditions, and lock effects.  After
analysis is complete, it reports accesses that violate declared locking
protections.

Locklint performs flow analysis over every retained function.  Analysis-root
reachability determines whether a function entry can be attributed to known
direct callers and therefore whether its lock conditions may be deferred.
Function summaries propagate through known direct calls.

Sparse parses C source and produces the intermediate representation used by
locklint.  Locklint registers callbacks to capture locking information during
preprocessing, then examines Sparse's symbols, expressions, instructions, and
control-flow graphs to perform its analysis.

The main phases are:

1. Parse locklint command-line options and leave ordinary compiler options for
   Sparse.
2. Register preprocessing hooks for locking annotations and assertions.
3. Create a synthetic translation-unit record for Sparse initialization,
   initialize Sparse, register declarations produced by initialization,
   resolve annotations while the initialization namespace is current, and
   process the initial symbols.
4. For each input file:
   1. create and make current a locklint translation-unit record;
   2. parse and evaluate the translation unit;
   3. register its file-scope internal-linkage declarations;
   4. resolve annotations while that translation unit's symbol namespaces are
      current;
   5. expand and linearize function definitions;
   6. associate retained records and relevant Sparse symbols with the current
      translation unit; and
   7. use Sparse's source-use walker to record function-pointer evidence from
      evaluated initializers and function bodies; and
   8. retain each function entrypoint for later checking.
5. After all files have been parsed:
   1. combine object and function declarations according to C linkage;
   2. scan call instructions and resolve direct callees as needed;
   3. resolve previously recorded function escapes to retained definitions
      and build their target index;
   4. assign additive conservative root reasons;
   5. propagate reachability from those roots through resolved direct calls;
   6. emit a requested call-graph audit;
   7. solve function lock-effect summaries;
   8. solve intraprocedural lock state;
   9. collect and propagate protected-access lock conditions; and
   10. emit diagnostics using the stable summaries.
6. Emit other requested development dumps.  `--dump-all` includes the
   whole-program call-graph audit.

Locklint deliberately delays diagnostics until summaries stabilize.  A
warning seen during an early pass could otherwise be invalidated by a later
callee summary.

## Data model overview

Locklint's data model has three layers.  Sparse owns the separately parsed
translation units and their intermediate representations.  Locklint records
the translation-unit provenance of retained Sparse objects.  A program-wide
semantic layer then combines declarations whose C linkage gives them one
identity and owns locking declarations, normalized object identities,
per-function state, and function summaries.

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

### Ownership boundary

Sparse owns the parsed program: symbols, types, expressions, entrypoints,
basic blocks, instructions, and the lists that connect them.  Locklint
borrows pointers to those objects while processing all translation units and
performing the final module analysis.  It does not copy the function IR or
CFG and does not free the borrowed objects.

The locklint records keep direct links back to the Sparse representation.
For example, `function_info` refers to its Sparse `entrypoint`, `block_info`
refers to a Sparse `basic_block`, and a temporary `call_audit` refers to a
Sparse `instruction`.  A `locklint_access` may refer to Sparse root, type, and
member symbols and to the source expression from which the access was
recovered.  Function-escape and function-pointer-activity records similarly
refer to the Sparse function, object, or member symbols observed by the
source-use walker.  Translation-unit provenance identifies the particular
parse that owns each borrowed object.

Locklint owns the information it adds around those references: translation
unit records, program-wide object and function indexes, root and reachability
state, pointer evidence, lock-state maps, and interprocedural summaries.
Source positions are copied by value where a durable ordering or diagnostic
origin is needed.  Annotation tokens are the notable deeper copy because
Sparse releases their preprocessing storage before all locklint resolution
and reporting is complete.

### Translation units and program-wide identities

Before calling `sparse(file)`, locklint creates a unique translation-unit
record for that input occurrence and makes it current.  Records captured or
created during that parse retain the translation-unit identity as provenance.
The identity is an allocated record or monotonic identifier, not merely a
pathname: parsing the same top-level pathname twice still creates two parse
occurrences.

Source location and translation-unit provenance are independent.  A position
identifies the physical file, line, and column containing syntax.  Provenance
identifies the top-level input whose preprocessing and parse produced the
corresponding Sparse object.  Both are required when a header is included by
multiple translation units.

After parsing, locklint maps relevant Sparse object declarations to canonical
object identities:

```text
local object or formal argument
    -> Sparse symbol identity

internal-linkage object
    -> (translation-unit identity, C identifier)

external-linkage object
    -> program-wide C identifier
```

Declarations within one translation unit are resolved before declarations are
combined across translation units.  Consequently:

```c
static int object;
extern int object;
```

declares one internal-linkage object in that translation unit.  An external
`object` in another translation unit has a different identity.

The program layer interns canonical object identities.  State and relation
records may continue to contain normalized accesses, but access equality uses
the canonical root identity rather than repeating declaration-by-declaration
comparisons.  Equivalent external declarations therefore share one root
identity without discarding their per-translation-unit source origins.  A
canonical identity may refer to multiple origins for diagnostics and
ambiguity reporting.

Functions remain represented by `function_info`.  Declarations and direct
calls are associated with the matching definition according to the same C
linkage boundaries, but object and function records are not forced into a
common abstraction.

### Headers and semantic duplication

Sparse preprocesses a header in the context of each translation unit that
includes it.  The same header pathname and line can produce different
declarations because of conditional macros, prior declarations, packing,
compiler options, or other preprocessing context.  Locklint therefore does
not merge declarations, annotations, or types solely by header pathname and
source position.

Deduplication occurs only after names have been resolved in their defining
translation units:

- declarations of the same external object map to one canonical object
  identity;
- equivalent normalized accesses compare through that shared identity and
  may later be interned as complete access identities;
- equivalent resolved protection relations may share one semantic relation
  while retaining all source origins; and
- a function definition and summary are stored once even when several
  translation units contain declarations for that function.

Type-scoped annotations and compound types initially remain
translation-unit-local.  C structure and union tags do not have linker
identity, and equal tag spelling or header origin does not prove that two
separately parsed types are compatible.  Cross-translation-unit operations
map members through canonical object or function boundaries using resolved
member identifiers and offsets; they do not globally intern a type by tag
name.

Raw annotation tokens are per-translation-unit observations.  They must
remain available until name resolution is complete because Sparse releases
preprocessing token storage.  A future memory-lifetime refinement may release
copied tokens after resolution while retaining compact source origins and
canonical protection relations.

### Locklint records

The main locklint-owned records relate as follows:

```text
translation units
  `- translation_unit
       `- retained Sparse records and source origins

object identities
  `- object_identity
       `- declaration source origins

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
       |- root-reason bit mask
       |- block_info list
       |    |- input state_entry list
       |    `- output state_entry list
       |- lock_condition list
       `- lock_transfer list

function-pointer observations
  |- function_escape
  `- function_pointer_activity

temporary call audit
  `- call_audit array for one function
```

The structures have these roles:

| Structure | Role and important relationships |
| --- | --- |
| `struct translation_unit` | One top-level input parse; provides provenance for retained records and internal-linkage identity |
| `struct object_identity` | Canonical identity for one object; records its linkage, identifier, optional owning translation unit, representative Sparse symbol, and declaration origins |
| `struct annotation` | One captured `_NOTE(...)`; owns copied tokens and refers to one lock reference and a list of data references |
| `struct annotation_token` | A durable copy of one preprocessing token from an annotation body |
| `struct annotation_ref` | One parsed and later resolved lock or data endpoint; refers to Sparse symbols and records replacement precedence |
| `struct assertion` | One recognized lock predicate captured from `ASSERT(...)` or `VERIFY(...)`; records source ranges and the asserted state |
| `struct locklint_access` | A normalized identity for an object or member access; refers to its canonical object identity or local root symbol, type, final member, cumulative offset, and source expression |
| `struct function_info` | Locklint's record for one function; refers to its Sparse entrypoint, embeds its automatic root-reason mask and function-index linkages, and owns its block state and summaries |
| `struct call_audit` | One temporary source-order entry for a live call instruction while dumping a function's calls |
| `struct function_escape` | One source observation that an exact function was used as a value; supplies root provenance through its source position and resolved target |
| `struct function_pointer_activity` | One initializer or function-body load or store involving a function pointer; a pointer copy is represented by its source load and destination store |
| `struct block_info` | Associates one Sparse basic block with reachability and its input and output lock-state maps |
| `struct state_entry` | One lock and its state in a block-state map; embeds a `locklint_access` identity |
| `struct lock_condition` | A caller-visible entry lock condition; a condition inferred from a protected access is associated with a formal data argument and either a relative lock or a preserved absolute lock root |
| `struct lock_transfer` | A function lock-effect summary for one formal lock and each possible input state |
| `struct transfer_block_info` | Temporary per-block state used while computing one lock transfer |

The program-wide object-identity, annotation, assertion, and function
collections
connect data captured from all input files.  Translation-unit provenance and
references to Sparse symbols and expressions tie those records back to the
particular parse and physical source position that produced them.

### Collections and indexes

Owning storage and lookup indexes are separate.  The illumos AVL
implementation does not allocate or own its nodes: an object stored in an AVL
tree embeds an `avl_node_t` linkage.  One linkage can belong to exactly one
tree.  An object indexed by several AVL trees therefore embeds one separately
named linkage for each tree.

An object does not embed linkage for an unbounded one-to-many relationship.
Such a relationship uses a separate association object.  For example, many
declaration symbols may bind to one `object_identity`; a symbol index stores
separate declaration-binding entries rather than requiring an
`object_identity` to embed one linkage per declaration.

The module-scope records use these collections:

| Object | Owning storage | Collections and indexes |
| --- | --- | --- |
| `function_info` | Individually allocated records in an owning linked list | Embeds `by_entrypoint` for an AVL keyed by Sparse entrypoint and `by_identity` for an AVL keyed by C function identity |
| `call_audit` | Temporary array allocated only while dumping one function | Sorted by source position and collection sequence with `qsort()`; no persistent index |
| `function_escape` | Individually allocated records in an owning linked list | Embeds `by_source` for deterministic source traversal and `by_target` for finding every escape reason associated with a resolved function |
| `function_pointer_activity` | Individually allocated records in an owning linked list, created only when a call-graph audit is requested | Embeds `by_source` for one source-ordered AVL; it has no target index because no exact target is known |
| root-reason kinds | A bit mask embedded in `function_info` | No separate collection; source-backed escape reasons refer to `function_escape` records |
| `translation_unit` | Existing process-lifetime sequence | No additional index in this increment |
| `object_identity` | Existing process-lifetime records | Existing identifier and declaration-symbol hash indexes remain unchanged in this increment |

Functions are inserted incrementally while translation units are processed.
Their owning list provides stable addresses and full-function traversal.  The
two function AVL indexes provide `O(log n)` insertion and lookup and ordered
range traversal for ambiguous external definitions.

Calls remain in Sparse's retained IR rather than separate persistent records.
Root classification, reachability, lock-summary propagation, and diagnostics
scan live call instructions and resolve their direct callees as needed.
`dump_function_calls()` collects only the current function's live calls into
a temporary array, sorts that array by source position, emits the audit, and
frees it.

Function-escape records are individually allocated and linked for ownership
and full traversal.  Their source and target AVL trees are secondary indexes
over the same stable records.  Expected escape counts are modest, so avoiding
one allocation per record is not justified without measurements.
Function-pointer activity records use the same individual-allocation and
linked-ownership model, but are created only for an explicit call-graph audit.
Their single AVL index supplies deterministic source traversal.  A different
allocation strategy should be considered only if real-module measurements
show that allocation overhead or fragmentation is significant.

Every AVL comparator uses a complete key that cannot compare distinct records
as equal.  A monotonically assigned sequence number is the final tie-breaker
where identifier, translation unit, source position, and pointer identity do
not already guarantee uniqueness.  Prefix lookup uses `avl_find()`,
`avl_nearest()`, and ordered traversal across the matching range:

- the function-identity key includes linkage class, identifier, owning
  translation unit for internal linkage, definition position, and sequence;
- the function-escape source key includes translation unit, source position,
  and sequence;
- the function-escape target key includes the resolved `function_info`,
  source position, and sequence; and
- function-pointer activity uses translation unit, source position, operation
  kind, and sequence.

Each `function_info` and resolved `function_escape` belongs to two AVL trees
and therefore embeds two `avl_node_t` fields.  An unresolved escape remains
only in the source tree.  Each `function_pointer_activity` belongs to one
tree.  At LP64 each linkage costs 24 bytes.  Detailed pointer activity is
retained only for an explicit audit so ordinary checking does not pay that
potentially large memory cost.

The existing object-identity hash tables serve mutable, exact-key lookups on
hot access-analysis paths and do not require ordered traversal.  The function
AVLs instead support incremental insertion, deterministic traversal, and
prefix ranges such as all external definitions of one identifier.  These
different access patterns justify retaining both collection mechanisms.

Likely future indexes should follow the same rules:

- an effective protection relation may need both declaration/replacement
  order and an AVL index by protected-data identity;
- caller-witness diagnostics may justify introducing retained call-site
  records and a reverse index by callee; and
- an AVL replacement for declaration-symbol object bindings would put the
  linkage in separate binding records, not in `object_identity`.

Those indexes are not part of the module-scope increment.  Their cardinality,
memory cost, and lookup frequency must be reviewed before their collection
forms are chosen.

### Ownership and lifetime

- Sparse owns symbols, expressions, entrypoints, blocks, and instructions.
- Locklint borrows those objects for the duration of the process.
- Locklint owns translation-unit and canonical object-identity records.
- Locklint copies `_NOTE` text and positions because Sparse frees per-file
  preprocessing tokens.
- Locklint allocates annotations, references, assertions, and copied `_NOTE`
  tokens for process lifetime.
- Locklint owns individually allocated, list-owned function,
  function-escape, and optional function-pointer-activity records and their
  AVL indexes.
- Temporary call-audit arrays are freed after each function is dumped.
- Function records, function-pointer observations, lock conditions, transfers,
  CFG state maps, and transfer-simulation blocks are freed after their
  analysis use ends.
- The implementation is a batch command, so process-lifetime metadata is
  intentional.  A future library or daemon interface would require explicit
  teardown and per-invocation ownership.

The illumos and macOS builds both compile locklint's local `avl.c` and use its
local illumos-compatible AVL headers.  Keeping the AVL layout compatible also
permits generic AVL inspection with MDB on illumos.

## Sparse integration

This section describes the Sparse interfaces and retained metadata on which
locklint depends.  The integration boundary consists of the following entry
points, hooks, and retained source metadata.  These facilities are
lock-domain-neutral and can be used by other Sparse clients.

### Frontend entry points

`sparse_initialize()` initializes Sparse, consumes Sparse/compiler options,
collects input file names, creates predefined declarations, and returns any
initial symbols.  Locklint processes those symbols before processing the
explicit file list.  Sparse processes command-line forced includes only once
during this initialization.  If that source introduces an internal-linkage
declaration, locklint rejects an invocation with multiple explicit inputs
rather than incorrectly sharing one instance across their translation units.

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
program-wide analysis.

## Source object and member identity

### `struct locklint_access`

`locklint_access` is the common identity used for memory accesses and locks:

| Field | Meaning |
| --- | --- |
| `root` | Sparse symbol at the root of the source expression; retained for source traversal and local identity |
| `object` | Canonical object identity for an internal- or external-linkage root, or `NULL` for a local root |
| `type` | Compound type reached from the root after stripping pointer and node wrappers |
| `member` | Final resolved member symbol, or `NULL` for a whole object |
| `offset` | Cumulative byte offset of the selected path from the root |
| `expr` | Retained source expression, when path traversal is still required |

The access equality relation is:

```text
same root identity + same final member identity + same cumulative offset
```

Local roots and formal arguments have no canonical object identity and use
Sparse symbol identity.  Internal-linkage roots map to an object identity
keyed by translation-unit identity and C identifier.  External roots map to
an object identity keyed by their program-wide C identifier.  Declarations
are first combined within their translation unit so that a later `extern`
declaration inherits the linkage of a visible earlier declaration as required
by C.

When Sparse returns a translation unit's top-level symbol list, locklint first
registers its internal-linkage declarations by identifier.  Resolving a later
file-scope or block-scope `extern` root consults that translation-unit table
before considering the declaration external.  This recovers effective C
linkage without relying on a particular redeclaration symbol's storage-class
modifiers or changing Sparse.

Members of canonical roots match by identifier because separately parsed
declarations have different Sparse member symbols.  File-static objects in
different translation units remain distinct even when their identifiers,
member identifiers, and layouts are equal.

The final member distinguishes fields, while the cumulative offset
distinguishes repeated or nested occurrences of a member.  The root
distinguishes separate objects.  `locklint_same_access()` implements this
relation for object-scoped annotations and lock-state maps.

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

Known cases require additional work:

1. Arrays use an offset-based identity but do not yet have a documented,
   tested policy for distinguishing all dynamic element expressions.
2. External declarations with the same identifier are canonicalized without
   first diagnosing incompatible object types across translation units.
3. A multi-input invocation cannot currently use initialization-time
   internal-linkage declarations, such as file-static objects or functions
   from a command-line forced include.  Sparse supplies only one parsed
   instance, so locklint rejects the invocation rather than merging distinct
   C identities.

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

- concrete object annotations by canonical object identity, final member, and
  root-relative offset; or
- type-scoped annotations by final member plus a successful
  `locklint_access_base()` owner/offset match.

It then constructs the concrete protecting lock identity.  A concrete lock
keeps its own root and offset.  A type-scoped lock is based at the matched
instance of the annotated data owner.

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

## Module scope, calls, and roots

For a call whose Sparse symbol already has an entrypoint, locklint uses that
definition directly.

For an unresolved non-static call, `find_external_function()` compares the
identifier text against all retained non-static definitions.  Exactly one
match is accepted.  Multiple matches are treated as ambiguous and diagnosed
at the call.

This identifier-based remapping is necessary because separate translation
units have separate Sparse symbol identities.

Each live call instruction is classified when it is scanned as one of:

- a resolved direct call, with one known callee;
- an unresolved external call, for which no retained definition is known;
- an ambiguous external call, for which several definitions could match; or
- an indirect call, for which the called expression does not identify one
  function.

Only resolved direct calls contribute to call-graph traversal in this
increment.  Unresolved and ambiguous calls remain explicit in audit output
rather than silently disappearing.  Indirect target inference is separate
follow-up work.

### Automatic root discovery

Root classification is conservative and automatic.  A function accumulates
every applicable root reason:

- **external linkage** - code outside the analyzed inputs may call it;
- **no known direct caller** - no resolved direct edge accounts for entry;
  and
- **function pointer escape** - the function is used as a value outside a
  resolved direct call.

Function-pointer escape includes implicit function-to-pointer conversion, not
only an explicit unary `&`.  Locklint therefore does not rely solely on
Sparse's `MOD_ADDRESSABLE`.  While each translation unit's evaluated symbols
remain available, it uses Sparse's generic `dissect()` source-use walker to
scan object initializers, including block-scope static initializers, and
function bodies.  A reported `U_R_AOF` use of a function establishes escape.
The walker marks the called expression with `U_CALL` in addition to its
pointer-read mode, so a function used only as a direct callee is not
misclassified as escaping and an indirect-call target is not duplicated as a
pointer load.  For example:

```c
static struct cb_ops cb_ops = {
        .cb_open = driver_open
};
```

classifies `driver_open` as a root and records the exact function use as
evidence.  A designated operation-table member may additionally produce a
store identified by aggregate type and member.  Indexed initializer
destinations are not currently retained.  Other function-pointer loads and
stores are recorded for audit even when they do not reveal an exact target.
A pointer copy appears as the independently observed source load and
destination store.  A direct call does not by itself make its callee's address
escape.

`MOD_ADDRESSABLE` remains a conservative fallback for an internal-linkage
function whose source use is not represented by recorded evidence.  Sparse
also marks ordinary external definitions addressable, but those definitions
are already roots and that modifier alone is not reported as separate escape
provenance.  An internal fallback reason has the function declaration as
provenance but may lack the position and destination of the operation that
caused the modifier.

Reachability from those roots is propagated through known direct calls.
Static functions reached only by known direct calls may defer protected-data
warnings to callers.

Every retained function is analyzed.  Root reachability determines whether a
lock condition can safely be deferred through known callers; it does not
select which function bodies are retained.

A function may defer a lock condition only when it is reachable from an
analysis root and has no root reason of its own.  A root, or a function not
reachable through the resolved graph, is an independent diagnostic boundary.
Self-recursion does not establish a direct caller for root classification.
A mutually recursive static component with no external, escaping, or
caller-free member may remain rootless; the audit marks its functions
unreachable so this boundary is visible.

### Call-graph audit

`--dump-callgraph` emits a deterministic development audit organized by
function.  It shows:

- the function and its translation unit, so same-named file-static functions
  remain distinguishable;
- whether the function is reachable from an analysis root;
- all root reasons and their source evidence;
- resolved direct edges;
- unresolved and ambiguous external calls;
- unresolved indirect calls; and
- exact function-valued uses and other function-pointer loads and stores;
  pointer copies appear as paired load and store uses.

The audit reports analysis boundaries without claiming indirect targets.
Unresolved indirect calls do not yet produce ordinary `--check-locks`
diagnostics; the explicit audit is the reporting interface for this
increment.  Target inference and policy for user-facing incomplete-analysis
diagnostics require evidence from real module audits.

The existing `--check-locks` diagnostic for an ambiguous external direct call
remains unchanged.  Audit records supplement rather than suppress ordinary
diagnostics.

Functions are ordered by translation-unit input order, source position, and
identifier.  Root reasons use a fixed reason-kind order followed by evidence
position; calls and function-pointer uses use source order.  Each reported
load or store appears once, so one pointer copy normally produces two entries.
An indirect call appears only as a call and not as a duplicate
function-pointer load.  These rules keep golden output stable and limit
redundant pointer evidence.

### Future explicit roots

Automatic discovery is additive and does not preclude user-supplied roots.
Two possible future interfaces are:

- command-line options for roots specific to one analysis invocation; and
- source annotations for entry points that are properties of the source
  module.

Both would add distinct root reasons to the same function record.  A source
annotation would resolve a file-static name in its defining translation unit.
A command-line selector for a file-static function would require
translation-unit qualification.  Missing or ambiguous explicit selections
must be errors.  The syntax and implementation of both interfaces remain
TBD.  Initially they would only add roots; suppressing an automatically
inferred root is a separate, more dangerous operation.

## Function entry lock conditions

A `lock_condition` summarizes lock state that a caller must establish on
function entry.  Conditions currently arise from protected accesses that a
caller may satisfy.  Each condition records:

- the formal argument containing the protected object;
- an absolute lock root, or `NULL` when the lock is relative to that argument;
- the lock member and offset;
- the protected data member; and
- a representative source position.

`collect_local_lock_conditions()` replays each reachable block from its stable
input state.  Every unsatisfied protected access rooted at a formal argument
becomes a lock condition.  A protecting lock rooted at the protected object is
recorded as argument-relative; any other root is preserved as an absolute
lock.  Deferral eligibility is applied later, while emitting diagnostics, to
decide whether a caller-satisfiable lock condition suppresses the local
warning.

`map_call_lock_condition()` maps the protected-data argument and protecting lock
separately.  It rebases a relative lock onto the caller's actual argument, but
uses a preserved absolute root unchanged.  The mapped data object determines
whether a lock condition can propagate through another formal argument and
whether its local diagnostic can be deferred.

`propagate_function_lock_conditions()` repeatedly maps callee formal arguments
to caller actual arguments and adds unsatisfied lock conditions to the
caller.  The pass iterates to a fixed point, including recursive call cycles.

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
2. checks direct-call lock conditions and invalid summarized effects;
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
| `main()` | Register hooks, drive per-file parsing/resolution, and start program-wide analysis |
| `locklint_init_include_path()` | Replace Sparse's default include-path initialization |

### Access identity: `access.c`

| Function | Responsibility |
| --- | --- |
| `find_root()` | Recover the root symbol from supported expression forms |
| `find_member()` | Find retained member metadata in an expression |
| `locklint_get_access()` | Construct normalized root/member/offset identity |
| `locklint_same_access()` | Compare accesses using local or external object identity |
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
| `resolve_function_escapes()` | Resolve exact function-valued uses after every retained definition has been indexed |
| `locklint_check_record_pointer_evidence()` | Use Sparse's source-use walker to record function-valued uses and optional function-pointer loads and stores while a translation unit is current |
| `classify_roots()` | Add every applicable conservative root reason |
| `mark_reachable()` | Propagate root reachability through resolved direct calls |
| `dump_function_calls()` | Temporarily collect and source-sort one function's live call instructions for audit |
| `dump_callgraph()` | Emit roots, calls, and function-pointer evidence for audit |
| `collect_local_transfers()` | Discover formal lock-effect candidates |
| `propagate_transfer_candidates()` | Carry effect candidates through calls |
| `simulate_transfer()` | Compute one transfer-table entry |
| `collect_local_lock_conditions()` | Summarize deferred protected accesses as caller lock conditions |
| `map_call_lock_condition()` | Map a lock condition's data object and relative or absolute lock at a call |
| `propagate_function_lock_conditions()` | Carry lock conditions through calls to a fixed point |
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

### Protected-access lock condition

```text
callee protected access is unsatisfied
    -> formal data access becomes a lock condition
    -> relative lock or absolute lock root is recorded
caller OP_CALL resolved to callee
    -> formal data object mapped to actual argument
    -> relative lock rebased to actual object
       or absolute lock root preserved unchanged
    -> held caller state satisfies lock condition
    -> otherwise warning is emitted or lock condition is deferred again
```

## Invariants

The current implementation relies on these invariants:

1. All macro hooks are registered before Sparse starts preprocessing input.
2. Raw annotation tokens needed after parsing are copied.
3. Annotation names are resolved while their defining translation unit's
   namespaces are current.
4. Every retained record has translation-unit provenance independent of its
   physical source position.
5. Every checked load, store, and call retains its source expression.
6. A lock-state key is stable for the lifetime of the analysis.
7. Same-named external roots share one canonical object identity;
   internal roots are qualified by translation-unit identity, and local roots
   retain Sparse symbol identity.
8. Header pathname and source position alone never establish semantic
   identity across translation units.
9. Absent lock-state entries mean definitely not held.
10. Only definitely held state satisfies a protected-access lock condition.
11. Lock-condition propagation maps the protected-data argument independently
   and never rebases an absolute lock root.
12. Assertions refine state but do not create effect summaries.
13. Interprocedural effects and lock conditions reach fixed points before
   diagnostics are emitted.
14. Multiple external function definitions with one identifier are ambiguous,
    not arbitrarily selected.
15. A later protection declaration replaces an earlier relation for the same
    resolved datum.
16. Every analysis root retains all independently established reasons.
17. A function used as a value outside a resolved direct call is a
    conservative root, including when the conversion occurs in a static
    initializer without explicit `&`.
18. Unresolved, ambiguous, and indirect calls remain visible in the call-graph
    audit and do not create invented call edges.

Changes that invalidate one of these invariants should update this document
and add a focused regression test.

## Current design limits and follow-up work

The implemented design remains intentionally narrow.  Important missing
areas include:

- general alias and nested-object identity;
- indirect-call and callback target resolution;
- explicit root configuration and source annotations;
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
