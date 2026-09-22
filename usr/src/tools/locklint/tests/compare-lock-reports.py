#!/usr/bin/env python3
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

"""
Compare protected-access reports from OSLL and new locklint during development.

Each parser reduces its input to source file, operation, protected datum, and
required lock.  OSLL functions and locklint source locations identify
distinct evidence within that key.  Locklint must provide at least one
distinct location for each OSLL function; additional report multiplicity is
informational.
"""

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass
import re
import sys
from pathlib import Path
from typing import Iterable, Optional


@dataclass(frozen=True, order=True)
class Finding:
    source: str
    operation: str
    datum: str
    lock: str
    region: str


@dataclass(frozen=True, order=True)
class FindingKey:
    source: str
    operation: str
    datum: str
    lock: str


class InputError(Exception):
    pass


OSLL_HEADER = re.compile(
    r"^\* Lock not protecting variable as asserted during (read|write)!$"
)
OSLL_VARIABLE = re.compile(r"^\s*variable = (\S+)\s*$")
OSLL_PROTECTOR = re.compile(r"^\s*protector = (\S+)\s*$")
OSLL_WHERE = re.compile(
    r"^\s*where = (\S+) \[([^,\]]+),([0-9]+)\]"
)
NEWLL_FINDING = re.compile(
    r"(?:^|: )([^:\n]+):([0-9]+):([0-9]+): "
    r"warning: locklint: protected member '([^']+)' "
    r"(read|modified) without holding '([^']+)' "
    r"\[unprotected-access\]$"
)


def short_name(name: str) -> str:
    if "::" in name:
        return name.split("::", 1)[1]
    return name


def osll_protector_name(value: str) -> str:
    return short_name(re.sub(r"\([^()]*\)$", "", value))


def finish_osll_finding(
    path: Path,
    line_number: int,
    operation: Optional[str],
    variable: Optional[str],
    protector: Optional[str],
    source: Optional[str],
    function: Optional[str],
    findings: list[Finding],
) -> None:
    if operation is None:
        return
    missing = []
    if variable is None:
        missing.append("variable")
    if protector is None:
        missing.append("protector")
    if source is None or function is None:
        missing.append("where")
    if missing:
        raise InputError(
            f"{path}:{line_number}: incomplete OSLL protected-access report: "
            f"missing {', '.join(missing)}"
        )
    findings.append(
        Finding(
            source,
            "read" if operation == "read" else "modify",
            short_name(variable),
            osll_protector_name(protector),
            function,
        )
    )


def parse_osll(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    operation: Optional[str] = None
    variable: Optional[str] = None
    protector: Optional[str] = None
    source: Optional[str] = None
    function: Optional[str] = None
    start_line = 0

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            header = OSLL_HEADER.match(line)
            if header is not None:
                finish_osll_finding(
                    path,
                    start_line,
                    operation,
                    variable,
                    protector,
                    source,
                    function,
                    findings,
                )
                operation = header.group(1)
                variable = None
                protector = None
                source = None
                function = None
                start_line = line_number
                continue
            if operation is None:
                continue
            match = OSLL_VARIABLE.match(line)
            if match is not None:
                if variable is not None:
                    raise InputError(
                        f"{path}:{line_number}: duplicate OSLL variable"
                    )
                variable = match.group(1)
                continue
            match = OSLL_PROTECTOR.match(line)
            if match is not None:
                if protector is not None:
                    raise InputError(
                        f"{path}:{line_number}: duplicate OSLL protector"
                    )
                protector = match.group(1)
                continue
            match = OSLL_WHERE.match(line)
            if match is not None:
                if source is not None or function is not None:
                    raise InputError(
                        f"{path}:{line_number}: duplicate OSLL where"
                    )
                function = match.group(1).rsplit(":", 1)[-1]
                source = Path(match.group(2)).name
                continue
            if line.startswith("* "):
                finish_osll_finding(
                    path,
                    start_line,
                    operation,
                    variable,
                    protector,
                    source,
                    function,
                    findings,
                )
                operation = None
                variable = None
                protector = None
                source = None
                function = None

    finish_osll_finding(
        path,
        start_line,
        operation,
        variable,
        protector,
        source,
        function,
        findings,
    )
    if not findings:
        raise InputError(f"{path}: no OSLL protected-access reports found")
    return findings


def parse_newll(path: Path) -> list[Finding]:
    findings: list[Finding] = []

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            if "[unprotected-access]" not in line:
                continue
            match = NEWLL_FINDING.search(line)
            if match is None:
                raise InputError(
                    f"{path}:{line_number}: unrecognized new locklint "
                    "protected-access report"
                )
            source, source_line, column, datum, operation, lock = match.groups()
            findings.append(
                Finding(
                    Path(source).name,
                    "read" if operation == "read" else "modify",
                    datum,
                    lock,
                    f"{source_line}:{column}",
                )
            )

    if not findings:
        raise InputError(
            f"{path}: no new locklint protected-access reports found"
        )
    return findings


def finding_key(finding: Finding) -> FindingKey:
    return FindingKey(
        finding.source,
        finding.operation,
        finding.datum,
        finding.lock,
    )


def describe(key: FindingKey) -> str:
    return (
        f"{key.operation} of '{key.datum}' requiring "
        f"'{key.lock}' in {key.source}"
    )


def compare(osll: Iterable[Finding], newll: Iterable[Finding]) -> bool:
    osll_counts: Counter[FindingKey] = Counter()
    newll_counts: Counter[FindingKey] = Counter()
    osll_regions: dict[FindingKey, set[str]] = defaultdict(set)
    newll_regions: dict[FindingKey, set[str]] = defaultdict(set)
    for finding in osll:
        key = finding_key(finding)
        osll_counts[key] += 1
        osll_regions[key].add(finding.region)
    for finding in newll:
        key = finding_key(finding)
        newll_counts[key] += 1
        newll_regions[key].add(finding.region)

    osll_keys = set(osll_counts)
    newll_keys = set(newll_counts)
    equivalent = True

    for finding in sorted(osll_keys - newll_keys):
        print(
            f"missing from new locklint: {describe(finding)} "
            f"(OSLL count {osll_counts[finding]})"
        )
        equivalent = False
    for key in sorted(newll_keys - osll_keys):
        print(
            f"unexpected in new locklint: {describe(key)} "
            f"(new locklint count {newll_counts[key]})"
        )
        equivalent = False
    for key in sorted(osll_keys & newll_keys):
        required = len(osll_regions[key])
        observed = len(newll_regions[key])
        if observed < required:
            print(
                f"insufficient new locklint locations: {describe(key)} "
                f"(OSLL functions {required}, "
                f"new locklint locations {observed})"
            )
            equivalent = False
        if osll_counts[key] != newll_counts[key]:
            print(
                f"count differs: {describe(key)} "
                f"(OSLL {osll_counts[key]}, "
                f"new locklint {newll_counts[key]})"
            )

    if equivalent:
        print(
            f"equivalent: {len(osll_keys)} protected-access groups "
            "occur on both sides"
        )
    return equivalent


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="compare OSLL and new locklint protected-access reports"
    )
    parser.add_argument("--from-osll", required=True, type=Path)
    parser.add_argument("--from-newll", required=True, type=Path)
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        osll = parse_osll(arguments.from_osll)
        newll = parse_newll(arguments.from_newll)
    except (InputError, OSError) as error:
        print(f"compare-lock-reports: {error}", file=sys.stderr)
        return 2
    return 0 if compare(osll, newll) else 1


if __name__ == "__main__":
    sys.exit(main())
