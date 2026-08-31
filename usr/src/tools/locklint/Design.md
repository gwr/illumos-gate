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
  `MUTEX_PROTECTS_DATA` is resolved into a data policy; an unsupported `_NOTE`
  body remains recorded but has no semantic effect.
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
  a local analysis fact rather than a persistent protection rule.
- **Protection relation** - An association between protected data and the
  lock required when accessing it.
- **Lock state** - Locklint's estimate that a lock is held, not held, or
  possibly held at a particular program point.
- **Function summary** - Information computed about a function for use by its
  callers, including required protection and lock or visibility transfers.
- **Lock effect** - The part of a function summary that describes how the
  function changes visible lock state.
- **Protection condition** - A function-entry requirement that selected data
  be protected by its mutex, invisibility, or absence of competing threads.
  It is part of a function summary and must be established by the caller.
- **Analysis root** - A function treated as externally reachable or as not
  safely attributable to a known caller.  Roots seed call-graph reachability
  and are boundaries at which unsatisfied protection conditions cannot simply be
  deferred to a known caller.
- **Root reason** - One independently established reason for treating a
  function as an analysis root.  A function may have several reasons, each
  with its own provenance.
- **Function-pointer evidence** - A source or intermediate-representation
  observation that a function is used as a value, or that a function pointer
  is initialized, loaded, stored, or copied.  Evidence supports conservative
  root classification and call-graph auditing; it does not by itself prove an
  indirect-call target.
- **Data policy** - The effective rules for one datum, with independent
  dimensions for its protection mechanism, whether unlocked reads are
  permitted, and whether writes are forbidden while exposed to competing
  threads.
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
determine lock, competition, and visibility state, function protection
conditions, and lock or visibility effects.  After analysis is complete, it
reports accesses that violate declared protection and exposure policies.

Locklint performs flow analysis over every retained function.  Analysis-root
reachability determines whether a function entry can be attributed to known
resolved callers and therefore whether its protection conditions may be
deferred.  Function summaries propagate through resolved calls.

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
      evaluated initializers and function bodies, and record exact targets
      from supported closed aggregate initializers; and
   8. retain each function entrypoint for later checking.
5. After all files have been parsed:
   1. combine object and function declarations according to C linkage;
   2. resolve recorded exact indirect targets and function escapes to retained
      definitions;
   3. scan call instructions and resolve direct or supported indirect callees
      as needed;
   4. assign additive conservative root reasons;
   5. propagate reachability from those roots through resolved calls;
   6. emit a requested call-graph audit;
   7. solve function lock and visibility transfer summaries;
   8. solve unified intraprocedural lock, competition, and visibility state;
   9. collect and propagate protection conditions; and
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
  `- function_record
       |- borrowed Sparse entrypoint and callgraph state
       `- function_info
            |- block_info list
            |    |- input analysis_state
            |    `- output analysis_state
            |- protection_condition list
            |- assumed_region list
            |- lock_transfer list
            `- visibility_transfer list

function-pointer observations
  |- function_escape
  |- indirect_target
  `- function_pointer_activity

temporary call audit
  `- call_audit array for one function
```

The structures have these roles:

| Structure | Role and important relationships |
| --- | --- |
| `struct translation_unit` | One top-level input parse; provides provenance for retained records and internal-linkage identity |
| `struct object_identity` | Canonical identity for one object; records its linkage, identifier, optional owning translation unit, representative Sparse symbol, and declaration origins |
| `struct annotation` | One captured `_NOTE(...)`; owns copied tokens, records its recognized kind and optional lock or scheme description, and refers to a list of data references |
| `struct annotation_token` | A durable copy of one preprocessing token from an annotation body |
| `struct annotation_ref` | One parsed and later resolved lock or data endpoint; refers to Sparse symbols and records replacement precedence |
| `struct assertion` | One recognized lock predicate captured from `ASSERT(...)` or `VERIFY(...)`; records source ranges and the asserted state |
| `struct locklint_access` | Dual source and computed-address identity for an object or member access; retains annotation and diagnostic provenance while optionally referring to the exact Sparse address pseudo and displacement |
| `struct locklint_member_path` | One immutable, interned member-path component; links to its containing path and records a member identifier, cumulative offset, and depth |
| `struct function_record` | Callgraph-private owning record for one function; contains its shared `function_info`, root and reachability bookkeeping, collection linkage, and AVL linkages |
| `struct function_info` | Shared semantic view of one function; refers to its translation unit and Sparse entrypoint and carries checker-owned block state and summaries |
| `struct call_audit` | One temporary source-order entry for a live call instruction while dumping a function's calls |
| `struct function_escape` | One source observation that an exact function was used as a value; supplies root provenance through its source position and resolved target |
| `struct indirect_target` | One exact target candidate for a member of a supported file-static constant aggregate; records translation unit, object, member, offset, source function, resolved target, and ambiguity |
| `struct function_pointer_activity` | One initializer or function-body load or store involving a function pointer; a pointer copy is represented by its source load and destination store |
| `struct block_info` | Associates one Sparse basic block with reachability and its input and output unified analysis states |
| `struct analysis_state` | The lock map, competition state and path-dependence flag, and per-region visibility facts at one CFG point |
| `struct state_entry` | One lock and its state in an analysis-state lock map; embeds a `locklint_access` identity |
| `struct visibility_entry` | One object region and its visible, invisible, or maybe-visible state |
| `struct protection_condition` | A caller-visible entry protection condition for formal-relative or absolute data, with an optional relative or absolute mutex |
| `struct assumed_region` | One function-wide region selected by `ASSUMING_PROTECTED` |
| `struct lock_transfer` | A function lock-effect summary for one formal lock and each possible input state |
| `struct transfer_block_info` | Temporary per-block state used while computing one lock transfer |
| `struct visibility_transfer` | A function visibility summary for one formal-relative or absolute region and each possible input visibility state |
| `struct visibility_transfer_block_info` | Temporary per-block state used while computing one visibility transfer |

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
| `function_record` | Individually allocated records in a callgraph-private owning linked list | Embeds `by_entrypoint` for an AVL keyed by Sparse entrypoint and `by_identity` for an AVL keyed by C function identity |
| `call_audit` | Temporary array allocated only while dumping one function | Sorted by source position and collection sequence with `qsort()`; no persistent index |
| `function_escape` | Individually allocated records in an owning linked list | Embeds `by_source` for deterministic source traversal and `by_target` for finding every escape reason associated with a resolved function |
| `indirect_target` | Individually allocated records in an owning linked list | Matched by translation unit, aggregate object, member, and offset; the supported exact case needs no separate index |
| `function_pointer_activity` | Individually allocated records in an owning linked list, created only when a call-graph audit is requested | Embeds `by_source` for one source-ordered AVL; it has no target index because no exact target is known |
| root-reason kinds | A bit mask embedded in `function_info` | No separate collection; source-backed escape reasons refer to `function_escape` records |
| `translation_unit` | Existing process-lifetime sequence | No additional index in this increment |
| `object_identity` | Existing process-lifetime records | Existing identifier and declaration-symbol hash indexes remain unchanged in this increment |

Functions are inserted incrementally while translation units are processed.
The callgraph-private owning list provides stable addresses.  Checker passes
traverse the ready function set through allocated callgraph iterators rather
than accessing that list.  The two function AVL indexes provide `O(log n)`
insertion and lookup and ordered range traversal for ambiguous external
definitions.

Calls remain in Sparse's retained IR rather than separate persistent records.
Root classification, reachability, lock-summary propagation, and diagnostics
scan live call instructions and resolve their callees as needed.
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

Each private `function_record` and resolved `function_escape` belongs to two
AVL trees and therefore embeds two `avl_node_t` fields.  The shared
`function_info` exposes none of those collection details.  An unresolved
escape remains only in the source tree.  Each `function_pointer_activity`
belongs to one tree.  At LP64 each linkage costs 24 bytes.  Detailed pointer
activity is retained only for an explicit audit so ordinary checking does not
pay that potentially large memory cost.

The existing object-identity hash tables serve mutable, exact-key lookups on
hot access-analysis paths and do not require ordered traversal.  The function
AVLs instead support incremental insertion, deterministic traversal, and
prefix ranges such as all external definitions of one identifier.  These
different access patterns justify retaining both collection mechanisms.

Likely future indexes should follow the same rules:

- effective data-policy lookup may need both declaration/replacement order and
  an AVL index by data identity;
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
- The callgraph owns individually allocated, list-owned function,
  function-escape, and optional function-pointer-activity records and their
  AVL indexes.
- The checker owns block maps, protection conditions, assumptions, lock
  transfers, and visibility transfers attached to each shared
  `function_info`.
- Temporary call-audit arrays are freed after each function is dumped.
- Checker attachments are freed before callgraph cleanup releases function
  records, pointer observations, and indexes.  Callgraph cleanup rejects open
  iterators or remaining checker attachments.
- Interned member paths are immutable during analysis and are freed after all
  requested checks and dumps complete.
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
| `path` | Interned source member path used for exact nested-member identity |
| `address_base` | Optional Sparse pseudo for the computed instruction address |
| `address_offset` | Signed byte displacement from `address_base` |

An access therefore retains two complementary identities.  Source identity
selects annotations, supports diagnostics, and remains available where no
lowered instruction exists.  Computed-address identity relates the actual
addresses used by memory instructions and call arguments.

When both accesses have computed addresses, the equality relation is:

```text
same address-base pseudo + same signed displacement
```

Otherwise equality falls back to the source relation:

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
distinguishes separate objects.  Exact computed addresses additionally
recognize local pointer copies and constant container conversions, and keep
unrelated symbolic array indices distinct.

`locklint_get_access()` recovers this identity from a retained expression.
`find_root()` searches through the expression operators supported by the
current model.  `find_member()` locates the retained member chain.
`member_offset()` sums `member_path_offset` across that chain.  The
instruction and call-argument constructors augment this source identity with
the corresponding Sparse pseudo.  Address normalization strips pointer casts
and folds exact constant additions and subtractions.  Other computed pseudos
remain opaque: repeated uses compare equal, while unrelated computations are
not guessed to be aliases.

`locklint_access_base()` attempts to determine where a type-scoped annotation
owner occurs within a concrete access.  It walks from the final member toward
the root, accumulating suffix offsets.  At a pointer root, it also recognizes
repeated owner-sized array elements.  A match supplies the base offset of the
annotated object within the concrete root.  Valid inline anonymous structures
and unions do not introduce a separately named owner type; Sparse's retained
path offset includes their promotion offsets, so an annotation on the
enclosing type uses the normal exact offset match.

### Identity limitations

The exact-address model is deliberately smaller than a general alias
analysis.  It recognizes relationships already made exact by Sparse, but does
not infer equality through unrelated pseudos or specialize a callee according
to relationships among its actual arguments.

Known cases require additional work:

1. Multiple formal arguments that receive one object require a
   context-sensitive or relational function summary.
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
- its recognized annotation kind;
- an optional parsed lock reference or explanatory scheme text;
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

For a mechanical lock, an object-scoped name may identify either a scalar or
an aggregate object.  A bare compound type cannot identify a particular lock
instance; a type-scoped lock must therefore name a member, such as
`record::lock`.

The parser accepts:

- bare object or type names;
- concrete paths such as `object.member`;
- type paths such as `type::member`;
- generated member lists such as `type::{ first second }`; and
- nested generators such as `type::{ nested.{ first second } }`.

The data-policy parser recognizes:

- `MUTEX_PROTECTS_DATA(lock, names)`;
- `SCHEME_PROTECTS_DATA("description", names)`;
- `DATA_READABLE_WITHOUT_LOCK(names)`; and
- `READ_ONLY_DATA(names)`.

The scheme description must be one quoted string.  It is explanatory text
rather than a mechanically checkable lock expression.  All four forms use
the same object/type name resolution and aggregate expansion for their data
lists.

### Resolution and expansion

`locklint_resolve_annotations()` processes only annotations not handled by an
earlier translation-unit pass.  Resolution occurs before locklint advances to
the next input file, so the current type and object namespaces remain
available.

Resolution performs these steps:

1. Resolve the lock name when the annotation names a mechanical lock.
2. Retain the explanatory text when the annotation names a scheme.
3. Resolve every data name.
4. Recursively expand structure-valued data into leaf members.
5. Skip the protecting lock member when recursively expanding the same
   object/type.
6. Record applicable overlaps with earlier resolved data references.
7. Mark the annotation resolved only if every required name succeeded.

Anonymous aggregate carriers are transparent during recursive expansion.
Named leaf members inherit the accumulated offset.

Aggregate expansion applies only to data references.  An aggregate lock object
retains the identity of the whole named object; its representation members are
not treated as separate locks.

### Replacement semantics

The effective data policy has three independent dimensions:

| Dimension | Values in this increment |
| --- | --- |
| protection mechanism | none, mutex, or explanatory scheme |
| unlocked-read permission | required protection or readable without lock |
| write-after-visibility policy | unrestricted or read-only |

For a selected datum, the last resolved protection-mechanism declaration in
source processing order wins.  A later scheme therefore replaces an earlier
mutex relation, and a later mutex replaces an earlier scheme.
`record_replacements()` links those earlier and later data references for
annotation dumps.

`DATA_READABLE_WITHOUT_LOCK` and `READ_ONLY_DATA` do not replace a protection
mechanism or each other.  They set independent policy dimensions and remain
effective if a later annotation changes the mechanism.  There are currently
no annotations that revoke either property.

### Matching an access

The policy lookup matches each resolved data reference against an access:

- concrete object annotations by canonical object identity, final member, and
  root-relative offset; or
- type-scoped annotations by final member plus a successful
  `locklint_access_base()` owner/offset match.

It combines the latest matching mechanism with all matching independent
properties.  For a mutex mechanism, it also constructs the concrete
protecting lock identity.  A concrete lock keeps its own root and offset.  A
type-scoped lock is based at the matched instance of the annotated data owner.
When the data access has an exact computed address, the required lock address
is derived by replacing the protected member's relative offset with the
protector's relative offset.  The lock consequently stays within the same
alias, array element, or recovered container.

### Effect on access checking

An explanatory scheme excludes matching data from ordinary mechanical-lock
checking because locklint cannot validate the convention described by the
annotation.  It remains visible in annotation dumps rather than being treated
as unprotected data.

For a mutex-protected access:

- a load requires the mutex unless unlocked-read permission is set; and
- a store always requires the mutex.

An allowed unlocked load does not create a caller protection condition.  An
unsatisfied store, or a load without unlocked-read permission, follows the
normal local-warning or protection-condition path.

`READ_ONLY_DATA` is parsed, resolved, retained, and displayed independently
of the selected protection mechanism.  Its exposure-dependent store rule is
described below.

## Visibility and competing-thread state

Locklint combines lock ownership, competing-thread state, and per-object
visibility in one flow-sensitive analysis state.  The same stable CFG state
is used to infer caller conditions and to replay diagnostics.

### Execution markers

Visibility and concurrency annotations appear within executable statements,
but the normal `_NOTE(...)` definition expands to nothing.  Matching a
captured source position with the nearest surviving instruction would be
incorrect in branches, empty blocks, and macros.

Locklint preserves recognized execution annotations as tagged,
zero-delta Sparse context instructions.  This extends the existing tagging
operation rather than adding a separate annotation opcode or representing
annotations as synthetic calls.

The existing two-argument form remains accepted:

```c
__context__(expression, delta);
```

The generic syntax and `OP_CONTEXT` representation support an optional client
tag:

```c
__context__(expression, delta, tag);
```

Existing context operations use tag zero and retain their current
lock-balance meaning.  A nonzero tag identifies a client-defined analysis
event.  Locklint tags use a delta of zero, so they do not alter Sparse's
ordinary context count.  Sparse preserves the instruction at its exact CFG
location and otherwise assigns no locking or visibility meaning to a
Locklint tag.

Locklint-only predefined forms expand recognized annotation bodies to
tagged `__context__` statements, and the `_NOTE` hook requests
preservation only for those forms.  For example, an object visibility marker
has this conceptual expansion:

```c
__context__((object), 0, LOCKLINT_CONTEXT_INVISIBLE);
```

Multiple selected expressions are retained as a comma-expression tree in
`context_expr`.  They are not evaluated and therefore create no synthetic
loads, stores, calls, or function arguments.  Locklint flattens that tree
when applying the transition.

The public Sparse pre-buffer facility is usable before
`sparse_initialize()`.  It previously tokenized added text before
`init_symbols()` installed canonical keyword identifiers, so a preloaded
`__context__` token was not later recognized as the reserved statement
keyword.  Sparse now defers tokenization of pre-buffer text until after symbol
and keyword initialization.  This is a generic initialization-ordering fix;
it contains no Locklint annotation names or policy.

Declaration annotations continue to disappear after their tokens are
captured.  During assertion analysis, a strong preprocessing definition
causes `ASSERT(NO_COMPETING_THREADS)` to retain the same tagged context marker
as `_NOTE(NO_COMPETING_THREADS_NOW)` instead of becoming the constant
expression supplied by system headers.  `_NOTE(NO_COMPETING_THREADS)` is
accepted as the same compatibility spelling.  The marker tag, retained
expression, source position, and translation-unit provenance provide all
information needed by Locklint; it does not infer execution order from source
positions.

The shared Sparse changes are limited to deferred pre-buffer tokenization,
the optional context tag in parsing and IR, preservation and display of that
tag, and focused generic Sparse validation.  Interpretation of every nonzero
tag remains with the client that introduced it.

### State domains

Competition is one flow-sensitive state for the executing path:

| State | Meaning |
| --- | --- |
| no competition | Other threads cannot access data used by this path |
| possible competition | Competition is not established consistently |
| competition present | Other threads may access the same data |

Function entry starts with possible competition.
`NO_COMPETING_THREADS_NOW` establishes no competition, and
`COMPETING_THREADS_NOW` establishes competition.  Merging different incoming
states produces possible competition and records that the uncertainty is
path-dependent.  The conservative possible state at function entry is not
itself described as a branch-dependent fact.

Visibility is flow-sensitive state associated with an object region:

| State | Meaning |
| --- | --- |
| invisible | Other threads cannot access the region |
| maybe visible | The region is invisible on only some incoming paths |
| visible | Other threads may access the region |

An unmentioned region is visible.  `NOW_INVISIBLE_TO_OTHER_THREADS` and
`NOW_VISIBLE_TO_OTHER_THREADS` set the selected regions to the corresponding
definite state.  A whole-object selection covers its members.  A member
selection affects that member and nested data within it but not its siblings.
When overlapping whole-object and member facts exist, the most specific
covering fact determines the queried state.  Updating a region removes
obsolete facts wholly covered by that update.

At a control-flow merge, Locklint computes the effective state of every
region distinguished on either incoming path.  Agreement remains definite;
disagreement becomes maybe visible.

### Protection and read-only decisions

A mutex-protected access is accepted when any one of these facts is definite:

1. the required mutex is held;
2. the selected datum is invisible; or
3. there are no competing threads.

If no fact is definite but at least one is path-dependent, the diagnostic
states that protection is not established on every path.  Otherwise the
access is diagnosed as unprotected.  `DATA_READABLE_WITHOUT_LOCK` continues
to accept matching loads independently.  Explanatory scheme protection
remains outside mechanical lock checking.

`READ_ONLY_DATA` is checked independently of the selected protection
mechanism.  A matching store is rejected only when the datum may currently be
visible to competing threads.  A definitely invisible datum or a definite
no-competition state permits initialization or private teardown writes.  The
policy is based on current exposure, not a permanent seal after first
publication; an explicit withdrawal can therefore permit later private
modification.

### Assertions, conditions, and calls

`ASSERT(NO_COMPETING_THREADS)` is treated as an advisory local transition,
equivalent to `_NOTE(NO_COMPETING_THREADS_NOW)`.  It suppresses subsequent
lock requirements by establishing no competition, but is not verified,
diagnosed as contradictory, or propagated as a caller condition.

Protected-access conditions express "this datum must be protected" rather
than only "this mutex must be held."  At a resolved call, the caller may
satisfy such a condition by holding the mapped mutex, by having the mapped
datum invisible, or by being in a no-competition state.  This preserves the
disjunction instead of arbitrarily choosing a lock requirement inside the
callee.  Conditions preserve the selected data member and offset and may
refer either to a formal-relative region or to an absolute canonical object.
An optional mutex is likewise relative or absolute.

`ASSUMING_PROTECTED(exprs)` is a function-entry contract regardless of where
its retained marker appears.  It creates a protection condition for each
selected expression and permits matching accesses throughout the function.
Callers must establish one of the accepted protection alternatives for each
mapped actual or absolute object.  A whole-object assumption covers its
descendants.  Invalid expressions are diagnosed during final replay.

Visibility changes to formal or absolute objects are inferred as function
summaries and mapped across resolved calls, like existing lock transfers.  Each
summary is an input-to-output table for the three visibility states, solved
across every reachable return and to a fixed point through resolved,
transitive, and recursive calls.  Canonical member paths preserve nested
regions, base offsets, and identity across translation units.  Overlapping
effects are applied from containing regions to leaves so a narrower result is
not erased by a broader one.  Visibility changes to unreturned local objects
require no summary.

Ordinary concurrency transitions remain local.  Locklint recognizes and
retains `NO_COMPETING_THREADS_AS_SIDE_EFFECT` and
`COMPETING_THREADS_AS_SIDE_EFFECT`, but does not yet change caller state or
validate return paths for them.  Historical LockLint describes nested
competing-thread regions, which the current three-state competition model
cannot represent faithfully.

Unresolved calls do not receive invented visibility or concurrency effects.
Mapping returned allocations and general aliases remains later object-identity
work.

### Focused validation

The tests cover:

- initialization under no competition and while an object is invisible;
- publication and withdrawal of whole objects and selected members;
- visibility and competition merges across branches;
- lock, invisibility, and no-competition alternatives at resolved calls;
- `ASSERT(NO_COMPETING_THREADS)` and `ASSUMING_PROTECTED`;
- direct, transitive, recursive, nested-object, and cross-translation-unit
  visibility summaries;
- retained concurrency side-effect markers without assigning semantics; and
- `READ_ONLY_DATA` stores before publication, while exposed, and after
  withdrawal.

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

Mutex recognition currently uses the operation name and argument identity.
It does not validate that the argument's declared type is a known mutex type.

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

1. apply summarized effects from a resolved call;
2. apply a direct mutex acquire or release; and
3. apply an assertion refinement.

Only `LOCK_HELD` satisfies a protected access.

## Module scope, calls, and roots

Callgraph construction and checking are separated by an explicit lifecycle:

```text
CONSTRUCTING -> RESOLVING -> READY -> CLEANED
```

While constructing, `locklint.c` adds each linearized function and records
function-address escapes, optional function-pointer activity, and exact target
candidates from supported static constant initializers.  Resolution is
deferred until all translation units have been processed so C identity,
ambiguous external definitions, exact indirect targets, roots, and
reachability are determined with complete program knowledge.

`callgraph_resolve()` closes construction and makes the graph ready.
Whole-function checker passes use opaque allocated iterators, and callee
queries and audit output are accepted only in the ready state.  Each opened
iterator must be closed.  After checker-owned attachments have been released,
`callgraph_cleanup()` requires that no iterators remain open and transitions
the graph to the cleaned state.  Construction cannot resume after resolution.

`callgraph.h` declares the module interface while keeping callgraph-owned
collections and indexes opaque.  `function_info.h` privately defines the
per-function representation shared by `callgraph.c` and `check.c`; it exposes
the Sparse function identity and checker attachments but not the owning list
or AVL linkages in the private `function_record`.

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
- a resolved indirect call through a supported exact aggregate member;
- an unresolved external call, for which no retained definition is known;
- an ambiguous external call, for which several definitions could match; or
- an indirect call, for which the called expression does not identify one
  supported exact target.

Resolved direct and indirect calls use one callee interface and contribute
equally to root classification, reachability, lock and visibility effects,
protection-condition propagation, and diagnostics.  Unresolved and ambiguous
calls remain explicit in audit output rather than silently disappearing.

### Exact static operations-vector targets

Locklint recognizes the deliberately narrow case of a file-static `const`
aggregate whose function-pointer member is initialized with one exact
function.  Sparse retains evaluated aggregate entries as positioned
initializer expressions.  Before those source structures are released,
locklint records the translation unit, aggregate object, member, byte offset,
source function symbol, and source position.

After all functions have been registered, the source function symbol is
resolved with the same C-linkage rules as a direct call.  An indirect call is
resolved only when its retained source expression names the same translation
unit, aggregate object, member, and offset, and the recorded target is unique
and unambiguous.  Matching both member and offset avoids conflating union
members that share storage.

The aggregate must remain a closed target source.  Mutable aggregate objects,
objects whose address escapes, unsupported initializer shapes, and
object/member entries with more than one possible target are not resolved.
Callback registration, pointer copies, mutable pointer variables, and indexed
target sets likewise remain outside this exact case.  Their calls stay
visible as unresolved indirect calls.

### Automatic root discovery

Root classification is conservative and automatic.  A function accumulates
every applicable root reason:

- **external linkage** - code outside the analyzed inputs may call it;
- **no known direct caller** - no resolved non-self edge accounts for entry;
  the audit retains this historical label for both direct and supported
  indirect edges; and
- **function pointer escape** - the function is used as a value outside a
  resolved call.

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
static const struct cb_ops cb_ops = {
        .cb_open = driver_open
};
```

records both the exact function use and the closed member-to-target mapping.
Calls through `cb_ops.cb_open` account for entry to `driver_open`, so the
initializer escape does not independently make `driver_open` a root.  If the
same function has any other unaccounted escape, that escape still supplies a
conservative root reason.

A designated operation-table member additionally produces a store identified
by aggregate type and member.  Indexed initializer destinations are not
currently retained as exact targets.  Other function-pointer loads and stores
are recorded for audit even when they do not reveal an exact target.  A
pointer copy appears as the independently observed source load and destination
store.  A direct call does not by itself make its callee's address escape.

`MOD_ADDRESSABLE` remains a conservative fallback for an internal-linkage
function whose source use is not represented by recorded evidence.  Sparse
also marks ordinary external definitions addressable, but those definitions
are already roots and that modifier alone is not reported as separate escape
provenance.  An internal fallback reason has the function declaration as
provenance but may lack the position and destination of the operation that
caused the modifier.

Reachability from those roots is propagated through resolved calls.  Static
functions reached only by resolved calls may defer protected-data
warnings to callers.

Every retained function is analyzed.  Root reachability determines whether a
protection condition can safely be deferred through known callers; it does not
select which function bodies are retained.

A function may defer a protection condition only when it is reachable from an
analysis root and has no root reason of its own.  A root, or a function not
reachable through the resolved graph, is an independent diagnostic boundary.
Self-recursion does not establish a caller for root classification.
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
- resolved indirect edges;
- unresolved and ambiguous external calls;
- unresolved indirect calls; and
- exact function-valued uses and other function-pointer loads and stores;
  pointer copies appear as paired load and store uses.

The audit labels supported inferred edges as `resolved-indirect`.  It reports
other indirect calls as unresolved without inventing targets.  Unresolved
indirect calls do not yet produce ordinary `--check-locks` diagnostics; the
explicit audit remains their reporting interface.  Policy for user-facing
incomplete-analysis diagnostics remains later work.

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

## Function entry protection conditions

A `protection_condition` summarizes data protection that a caller must
establish on function entry.  Conditions arise from protected accesses that a
known caller may satisfy and from `ASSUMING_PROTECTED` contracts.  Each
condition records:

- a formal argument or absolute canonical root containing the protected data;
- the selected data member and offset;
- whether a known mutex is available;
- a relative or absolute mutex identity when available; and
- a representative source position.

`collect_local_protection_conditions()` replays each reachable block from its
stable input state.  Every unsatisfied protected access with a caller-mappable
identity becomes a protection condition.  It also records function-wide
assumed-protection regions.  A mutex rooted at the protected object is
recorded as data-relative; any other canonical root is preserved as absolute.
Deferral eligibility is applied later, while emitting diagnostics, to decide
whether a caller-satisfiable condition suppresses the local warning.

`map_call_protection_condition()` maps the protected datum and optional mutex
separately.  It rebases a relative mutex onto the caller's actual argument,
but uses a preserved absolute root unchanged.  The mapped data object
determines whether an unsatisfied condition can propagate through another
formal argument.

`propagate_function_protection_conditions()` repeatedly maps callee conditions
to caller actual or absolute objects and adds unsatisfied conditions to the
caller.  The pass iterates to a fixed point, including recursive call cycles.
At a call site, definite mutex ownership, definite invisibility, or definite
absence of competition satisfies the condition.

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

1. checks `READ_ONLY_DATA` against current exposure;
2. checks a protected memory access against mutex, visibility, competition,
   and assumed-protection alternatives;
3. checks resolved-call protection conditions and invalid summarized lock
   effects, then applies summarized lock and visibility transfers;
4. checks direct acquire/release validity and applies the operation; and
5. applies assertion, competition, and visibility transitions.

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
| `process_symbols()` | Expand symbols, create entrypoints, add functions to the callgraph, and emit requested dumps |
| `main()` | Register hooks, drive per-file parsing/resolution, and start program-wide analysis |
| `locklint_init_include_path()` | Replace Sparse's default include-path initialization |

### Access identity: `access.c`

| Function | Responsibility |
| --- | --- |
| `find_root()` | Recover the root symbol from supported expression forms |
| `find_member()` | Find retained member metadata in an expression |
| `locklint_get_access()` | Construct normalized object and canonical member-path identity |
| `locklint_get_instruction_access()` | Add the normalized address used by a load or store |
| `locklint_get_call_argument_access()` | Add the normalized address passed as a call argument |
| `locklint_rebase_access()` | Compose a callee-relative member path onto a caller object |
| `locklint_same_access()` | Prefer exact computed-address equality, falling back to source identity |
| `locklint_access_contains()` | Test whole-object and nested-member containment |
| `locklint_access_base()` | Map a type-scoped relation into a concrete embedded object |
| `locklint_show_access()` | Display a source-oriented access path |

### Annotations: `annotations.c`

| Function | Responsibility |
| --- | --- |
| `capture_annotation()` | Copy `_NOTE` argument tokens before expansion/token release |
| `locklint_resolve_annotations()` | Process newly captured annotations in the current translation unit |
| `locklint_data_policy()` | Combine the effective policy dimensions and construct a concrete mechanical lock when required |

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

### Callgraph: `callgraph.c`

| Function | Responsibility |
| --- | --- |
| `callgraph_record_pointer_evidence()` | Use Sparse's source-use walker to record function-valued uses and optional function-pointer loads and stores, then record supported exact aggregate targets while a translation unit is current |
| `callgraph_add()` | Retain a function and add its Sparse and C-identity indexes during construction |
| `callgraph_resolve()` | Resolve identities and exact targets, classify roots, propagate reachability, and make the complete graph ready |
| `callgraph_iter_open()`, `callgraph_iter_next()`, `callgraph_iter_close()` | Allocate, advance, and dispose an opaque cursor over the ready function set |
| `callgraph_callee()` | Resolve one direct call or supported exact indirect call through the common semantic edge interface |
| `callgraph_ambiguous_callee()` | Report whether a direct call has multiple matching external definitions |
| `callgraph_dump()` | Emit the deterministic callgraph audit to a caller-supplied stream |
| `callgraph_cleanup()` | Verify iterator and attachment lifetimes, then release all callgraph-owned records and indexes |

### Checker: `check.c`

| Function | Responsibility |
| --- | --- |
| `analyze_blocks()` | Solve unified intraprocedural lock, competition, and visibility state |
| `run_lock_checks()` | Order lock and visibility transfer solving, block analysis, protection-condition propagation, and diagnostics |
| `locklint_check_all()` | Resolve the callgraph, order and iterate the complete analysis, release checker attachments, and clean up the callgraph |

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
    -> locklint_data_policy combines effective policy dimensions
    -> scheme-protected access is excluded from mechanical checking
    -> unlocked-readable load is accepted
    -> otherwise current protection alternatives are queried
    -> warning is emitted unless mutex ownership, invisibility,
       no competition, or an assumed-protection contract applies
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

### Direct callee visibility transfer

```text
callee visibility candidate discovered
    -> transfer table solved for INVISIBLE, MAYBE, and VISIBLE inputs
    -> every reachable return contributes to the output
caller OP_CALL resolved to callee
    -> callee-relative member path composed onto caller actual object
       or absolute object identity preserved unchanged
    -> current caller visibility selects transfer-table entry
    -> overlapping results collected from the original caller state
    -> containing regions installed before narrower leaves
```

### Protected-access condition

```text
callee protected access is unsatisfied
    -> formal-relative or absolute data becomes a protection condition
    -> optional relative or absolute mutex is recorded
caller OP_CALL resolved to callee
    -> formal data object mapped to actual argument
    -> relative lock rebased to actual object
       or absolute lock root preserved unchanged
    -> mutex, invisibility, or no competition satisfies the condition
    -> otherwise warning is emitted or the condition is deferred again
```

## Kernel and user test configurations

Readers-writer lock fixtures follow the normal illumos `_KERNEL` source
split.  In the default configuration they use the user-level `rwlock_t`
interface:

- `rw_rdlock()` for reader acquisition;
- `rw_wrlock()` for writer acquisition; and
- `rw_unlock()` for release.

With `-D_KERNEL`, the same fixtures use `krwlock_t`, `rw_enter()` with the
appropriate reader or writer mode, and `rw_exit()`.

Locklint does not infer whether a translation unit is kernel or user code.
Sparse preprocesses the source using the supplied compiler options, and
locklint recognizes the operations present in the selected branch.  The test
harness therefore analyzes each rwlock fixture both with and without
`-D_KERNEL`.

Normal automated fixtures are stand-alone.  They provide minimal local
declarations instead of including installed platform system headers, so
macOS and illumos runs present the same semantic input to Sparse.  A
multi-translation-unit fixture may use a header stored with the fixture.
System-header includes selected only by an optional compatibility branch are
manual comparison plumbing and are not dependencies of the automated suite.

Object-identity characterization uses two complementary fixtures.
`identity-aliases.c` covers direct aliases, array elements, and constant
container recovery.  `identity-formals.c` is analyzed both normally and with
`FORMAL_ALIAS_DIFFERENT`, giving each run one call whose actual arguments are
either equal or distinct.

Small fixture macros keep the protected accesses, control flow, and expected
ownership semantics common between configurations while expanding to the
actual interface calls.  Variant-specific cases are used only where one
interface has no equivalent, such as a nonconstant `rw_enter()` mode or the
kernel `RW_LOCK_HELD()` predicate.  Separate state golden files preserve
those intentional differences; common interprocedural findings use one
shared golden file.

## Invariants

The current implementation relies on these invariants:

1. All macro hooks are registered before Sparse starts preprocessing input.
2. Raw annotation tokens needed after parsing are copied.
3. Annotation names are resolved while their defining translation unit's
   namespaces are current.
4. Every retained record has translation-unit provenance independent of its
   physical source position.
5. Every checked load, store, and call retains its source expression.
6. Lock and visibility keys are stable for the lifetime of the analysis;
   immutable member paths are interned and shared by copied accesses.
7. Same-named external roots share one canonical object identity;
   internal roots are qualified by translation-unit identity, and local roots
   retain Sparse symbol identity.
8. Header pathname and source position alone never establish semantic
   identity across translation units.
9. Absent lock-state entries mean definitely not held.
10. A protected access or entry condition is satisfied by any definite
    protection alternative: mutex ownership, invisibility, no competition, or
    a matching function-wide assumed-protection region.
11. Protection-condition propagation maps the protected-data identity
    independently and never rebases an absolute data or mutex root.
12. Lock assertions refine local lock state without creating effects;
    `ASSERT(NO_COMPETING_THREADS)` is an advisory local competition
    transition and does not create a caller condition.
13. Interprocedural lock and visibility effects and protection conditions
    reach fixed points before diagnostics are emitted.
14. Multiple external function definitions with one identifier are ambiguous,
    not arbitrarily selected.
15. A later protection declaration replaces an earlier relation for the same
    resolved datum.
16. Protection mechanism, unlocked-read permission, and read-only status are
    independent data-policy dimensions.
17. `READ_ONLY_DATA` rejects stores only when the selected datum may currently
    be visible to competing threads; definite invisibility or no competition
    permits the store.
18. Every analysis root retains all independently established reasons.
19. A function used as a value outside a resolved call is a conservative root,
    except for a closed static initializer escape accounted for by its exact
    resolved indirect edges.
20. An exact indirect target matches translation unit, aggregate object,
    member, and offset; unsupported, unresolved, and ambiguous calls remain
    visible in the call-graph audit and do not create invented call edges.
21. A structure-valued lock retains whole-object identity; recursive aggregate
    expansion applies only to protected data.
22. Callgraph mutation is confined to construction; iteration, callee queries,
    and audit output require the ready state.
23. Every callgraph iterator is explicitly closed, and checker attachments are
    released before callgraph-owned function records.
24. Exact address identity strips only pointer casts and constant pointer
    displacements; unrelated computed pseudos are never assumed equal.
25. A type-scoped protector is rebased from the observed data address so it
    remains within the same alias, array element, or recovered container.

Changes that invalidate one of these invariants should update this document
and add a focused regression test.

## Current design limits and follow-up work

The implemented design remains intentionally narrow.  Important missing
areas include:

- context-sensitive formal aliases, general pointer relationships, and
  identities reached through returned pointers;
- indirect targets from mutable pointers, escaped tables, callback
  registration, pointer copies, ambiguous assignments, and indexed target
  sets;
- explicit root configuration and source annotations;
- declared competition side-effect semantics and nested competition regions;
- explicit lock-side-effect annotation contracts;
- optional, configurable validation of declared lock types;
- condition waits, try-locks, upgrades, and downgrades;
- lock-order analysis; and
- stable diagnostic identifiers, suppressions, and provenance.

These limitations should remain visible here as the implementation evolves.
When a limitation is removed, its replacement design, invariants, and action
sequence should be documented in the same change.
