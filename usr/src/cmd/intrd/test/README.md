# intrd Test Framework

This directory contains the test suite for `intrd`, the
interrupt distribution daemon.
The test data is implementation-independent, though some of the
test framework is intimate with the daemon implementation so it
can drive behavior and observe actions.

---

## Running the Tests

```sh
cd usr/src/cmd/intrd/test
make                  # regenerate .kstats files from .py specs if needed
sh run-tests.sh       # run all tests, verifying correct outputs
```

To run a single test:

```sh
sh run-tests.sh case02.kstats
```

Each test prints `PASS` or `FAIL`.  On failure the diff against the reference
output is shown.

---

## How a Test Works

Each test case consists of three files:

| File | Purpose |
|------|---------|
| `caseNN.py` | Scenario spec: Python dict read by `gen_case.py` to produce the `.kstats` file |
| `caseNN.kstats` | Synthetic kstat input in `kstat -p` format; generated from the `.py` spec |
| `caseNN.ref` | Reference output captured from a known-good run; the test passes if output matches |

`run-tests.sh` runs:
```sh
../intrd-test caseNN.kstats > caseNN.out 2>&1
diff caseNN.ref caseNN.out
```

`intrd-test` is the test-mode build of `intrd`.  It reads snapshots from the
`.kstats` file one at a time, and provides stub implementations of `syslog()`,
`intr_move()`, and `is_apic()`.  The stubs filter syslog output to suppress
low-level debug noise, keeping only the high-level decision messages that
constitute meaningful test output.

What each test case covers is described in the comment block at the top of its
`.py` file.

---

## Input File Format

`.kstats` files use the `kstat -p` text format: tab-separated
`module:instance:name:stat<TAB>value` lines, grouped into snapshots:

```
# sample 1  t=1000.0
cpu:0:sys:cpu_nsec_idle     108790000000
cpu:0:sys:cpu_nsec_user     1100000000
cpu:0:sys:cpu_nsec_kernel   110000000
cpu:0:sys:crtime            1.0
cpu:0:sys:snaptime          1000.0
cpu_info:0:cpu_info0:state  on-line
pci_intrs:1:npe:cpu         0
pci_intrs:1:npe:type        msix
pci_intrs:1:npe:time        110000000
pci_intrs:1:npe:pil         6
pci_intrs:1:npe:ino         32
pci_intrs:1:npe:buspath     /pci@0,0
pci_intrs:1:npe:name        igb0
pci_intrs:1:npe:crtime      2.0
pci_intrs:1:npe:snaptime    1000.0

# sample 2  t=1010.0
...
```

Key points:

- All `time` and `nsec` values are **cumulative since boot**.  intrd computes
  deltas between consecutive snapshots.
- Provide enough samples to fill intrd's 60-second rolling statistics window.
  At the default 10-second sleep interval, that means at least 7 samples; use
  10–12 for margin.
- The optional pseudo-kstat `intrd_test:0:config:is_apic<TAB>1` anywhere in the
  file sets the return value of `is_apic()` for the whole run (default: 0).
- The optional pseudo-kstat `intrd_test:0:config:fail_move_count<TAB>N` causes
  the next N calls to `intr_move()` to return failure, regardless of which
  interrupt is being moved.  Use this to test the `do_reconfig FAILED` path and
  recovery after the statistics window refills.
- Interrupt `time` values in synthetic inputs are scaled up from real-system
  magnitudes in order to exercise intrd's decision logic.  Real systems captured
  while idle show near-zero interrupt time.

### Required fields per snapshot

| kstat path | Fields needed |
|---|---|
| `cpu:<id>:sys` | `cpu_nsec_idle`, `cpu_nsec_user`, `cpu_nsec_kernel`, `crtime`, `snaptime` |
| `cpu_info:<id>:cpu_info<id>` | `state` |
| `pci_intrs:<inum>:<nexus>` | `cpu`, `type`, `time`, `pil`, `ino`, `buspath`, `name`, `crtime`, `snaptime` |

---

## Adding a New Test Case

### Step 1 — Write the spec

Copy an existing `caseNN.py` as a starting point.  For most cases the spec is
a Python dict consumed by `gen_case.py`.  At minimum set:

```python
num_cpus   = 4          # number of online CPUs
num_samples = 12        # snapshots to generate (covers 60-sec window plus margin)
interval   = 10.0       # seconds between snapshots
statslen   = 60         # must match intrd's $statslen (normally 60)

interrupts = [
    {'inum': 1, 'nexus': 'npe', 'cpu': 0, 'type': 'msix',
     'pil': 6, 'ino': 32, 'buspath': '/pci@0,0', 'name': 'igb0',
     'load': 0.55},   # fraction of one CPU's total time
    ...
]
```

The `load` value is the fraction of one CPU's total time this interrupt consumes
each interval.  `gen_case.py` converts it to cumulative nsec values.

To model mid-run changes (CPU hotplug, interrupt reassignment, varying load),
use the `per_sample(n, spec)` hook — see existing cases for examples.

For cases that need finer per-sample control (e.g. varying `pci_intrs`
snaptime independently of the CPU snaptime), write a standalone Python script
instead of a plain dict.  Import the helpers from `gen_case`:

```python
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_case import init_interrupts, make_state, gen_sample
```

See `case16.py` for an example of this pattern.

Add a comment block at the top of the file explaining what intrd behavior the
test exercises and how the scenario forces that code path.  Do not simply
restate the load values visible in the spec body.

### Step 2 — Generate the kstats file

```sh
make caseNN.kstats
# or manually:
python3 gen_case.py caseNN.py > caseNN.kstats
```

### Step 3 — Capture the reference output

Run the test and inspect the output:

```sh
sh run-tests.sh caseNN.kstats
# if the output looks correct, capture it:
../intrd-test caseNN.kstats > caseNN.ref 2>&1
```

Verify the `.ref` file shows the decisions you expected (syslog messages,
`intrmove` calls).  If the output looks wrong, fix the spec and repeat.

### Step 4 — Run all tests

```sh
sh run-tests.sh
```

All existing tests must continue to pass.

---

## Updating a Reference File

If a change to `intrd` intentionally alters behavior, regenerate the `.ref`:

```sh
../intrd-test caseNN.kstats > caseNN.ref 2>&1
```

Review the `*.ref` diff carefully before committing.

---

## Files

| File | Purpose |
|------|---------|
| `run-tests.sh` | Test runner |
| `gen_case.py` | Generates `.kstats` files from `.py` scenario specs, and module functions for case16.py |
| `caseNN.py` | Scenario spec (Python dict or script); describes the test at its top |
| `caseNN.kstats` | Generated kstat input; produced by `make` (not committed) |
| `caseNN.ref` | Reference output; committed; defines expected behavior |
| `Makefile` | Builds `.kstats` files; runs tests via `make test` |
