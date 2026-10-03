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
import csv
from dataclasses import dataclass
import re
import sys
from pathlib import Path
from typing import Iterable, Optional


@dataclass(frozen=True, order=True)
class Finding:
    source: str
    line: int
    column: Optional[int]
    function: Optional[str]
    operation: str
    datum: str
    lock: str


@dataclass(frozen=True, order=True)
class FindingKey:
    source: str
    operation: str
    datum: str
    lock: str


@dataclass(frozen=True, order=True)
class LocationFindingKey:
    source: str
    line: int
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
OSLL_REFERENCE_HEADER = [
    "analyzer",
    "source",
    "line",
    "column",
    "function",
    "operation",
    "datum",
    "lock",
]
NEWLL_FINDING = re.compile(
    r"(?:^|: )([^:\n]+):([0-9]+):([0-9]+): "
    r"warning: protected member '([^']+)' "
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
    source_line: Optional[int],
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
    if source is None or source_line is None or function is None:
        missing.append("where")
    if missing:
        raise InputError(
            f"{path}:{line_number}: incomplete OSLL protected-access report: "
            f"missing {', '.join(missing)}"
        )
    findings.append(
        Finding(
            source,
            source_line,
            None,
            function,
            "read" if operation == "read" else "modify",
            short_name(variable),
            osll_protector_name(protector),
        )
    )


def is_osll_reference(path: Path) -> bool:
    with path.open(encoding="utf-8", errors="replace") as stream:
        for raw_line in stream:
            line = raw_line.rstrip("\n")
            if line and not line.startswith("#"):
                return line.split("\t", 1)[0] == "analyzer"
    return False


def parse_osll_reference(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    saw_header = False

    with path.open(encoding="utf-8", errors="replace", newline="") as stream:
        reader = csv.reader(stream, delimiter="\t")
        for line_number, fields in enumerate(reader, 1):
            if not fields or (len(fields) == 1 and not fields[0]):
                continue
            if fields[0].startswith("#"):
                continue
            if not saw_header:
                if fields != OSLL_REFERENCE_HEADER:
                    raise InputError(
                        f"{path}:{line_number}: invalid OSLL reference header"
                    )
                saw_header = True
                continue
            if len(fields) != len(OSLL_REFERENCE_HEADER):
                raise InputError(
                    f"{path}:{line_number}: expected "
                    f"{len(OSLL_REFERENCE_HEADER)} tab-separated fields"
                )
            (
                analyzer,
                source,
                source_line,
                column,
                function,
                operation,
                datum,
                lock,
            ) = fields
            if analyzer != "osll":
                raise InputError(
                    f"{path}:{line_number}: expected analyzer 'osll'"
                )
            if column:
                raise InputError(
                    f"{path}:{line_number}: OSLL column must be empty"
                )
            if operation not in ("read", "modify"):
                raise InputError(
                    f"{path}:{line_number}: invalid operation '{operation}'"
                )
            if not all((source, source_line, function, datum, lock)):
                raise InputError(
                    f"{path}:{line_number}: incomplete OSLL reference record"
                )
            try:
                parsed_line = int(source_line)
            except ValueError as error:
                raise InputError(
                    f"{path}:{line_number}: invalid source line "
                    f"'{source_line}'"
                ) from error
            if parsed_line <= 0:
                raise InputError(
                    f"{path}:{line_number}: source line must be positive"
                )
            findings.append(
                Finding(
                    source,
                    parsed_line,
                    None,
                    function,
                    operation,
                    datum,
                    lock,
                )
            )

    if not saw_header:
        raise InputError(f"{path}: no OSLL reference header found")
    if not findings:
        raise InputError(f"{path}: no OSLL protected-access reports found")
    return findings


def parse_osll_raw(path: Path) -> list[Finding]:
    findings: list[Finding] = []
    operation: Optional[str] = None
    variable: Optional[str] = None
    protector: Optional[str] = None
    source: Optional[str] = None
    source_line: Optional[int] = None
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
                    source_line,
                    function,
                    findings,
                )
                operation = header.group(1)
                variable = None
                protector = None
                source = None
                source_line = None
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
                source_line = int(match.group(3))
                continue
            if line.startswith("* "):
                finish_osll_finding(
                    path,
                    start_line,
                    operation,
                    variable,
                    protector,
                    source,
                    source_line,
                    function,
                    findings,
                )
                operation = None
                variable = None
                protector = None
                source = None
                source_line = None
                function = None

    finish_osll_finding(
        path,
        start_line,
        operation,
        variable,
        protector,
        source,
        source_line,
        function,
        findings,
    )
    if not findings:
        raise InputError(f"{path}: no OSLL protected-access reports found")
    return findings


def parse_osll(path: Path) -> list[Finding]:
    if is_osll_reference(path):
        return parse_osll_reference(path)
    return parse_osll_raw(path)


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
                    int(source_line),
                    int(column),
                    None,
                    "read" if operation == "read" else "modify",
                    datum,
                    lock,
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


def describe_location(key: LocationFindingKey) -> str:
    return (
        f"{key.operation} of '{key.datum}' requiring "
        f"'{key.lock}' at {key.source}:{key.line}"
    )


def compare_semantic(
    osll: Iterable[Finding],
    newll: Iterable[Finding],
    expect: str,
    reviewed_newll_only: Optional[set[FindingKey]],
) -> bool:
    osll_counts: Counter[FindingKey] = Counter()
    newll_counts: Counter[FindingKey] = Counter()
    osll_regions: dict[FindingKey, set[str]] = defaultdict(set)
    newll_regions: dict[FindingKey, set[str]] = defaultdict(set)
    for finding in osll:
        key = finding_key(finding)
        osll_counts[key] += 1
        if finding.function is not None:
            osll_regions[key].add(finding.function)
    for finding in newll:
        key = finding_key(finding)
        newll_counts[key] += 1
        newll_regions[key].add(f"{finding.line}:{finding.column}")

    osll_keys = set(osll_counts)
    newll_keys = set(newll_counts)
    newll_only = newll_keys - osll_keys
    equivalent = True

    for finding in sorted(osll_keys - newll_keys):
        print(
            f"missing from new locklint: {describe(finding)} "
            f"(OSLL count {osll_counts[finding]})"
        )
        equivalent = False
    if expect == "equivalent":
        for key in sorted(newll_only):
            print(
                f"unexpected in new locklint: {describe(key)} "
                f"(new locklint count {newll_counts[key]})"
            )
            equivalent = False
    elif reviewed_newll_only is not None:
        for key in sorted(newll_only - reviewed_newll_only):
            print(
                f"unreviewed in new locklint: {describe(key)} "
                f"(new locklint count {newll_counts[key]})"
            )
            equivalent = False
        for key in sorted(reviewed_newll_only - newll_only):
            print(
                "reviewed native-only group no longer reported: "
                f"{describe(key)}"
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
        if expect == "osll-covered":
            suffix = "" if len(newll_only) == 1 else "s"
            if reviewed_newll_only is None:
                print(
                    f"covered: {len(osll_keys)} OSLL protected-access "
                    f"groups occur in new locklint; {len(newll_only)} new "
                    f"locklint-only group{suffix}"
                )
            else:
                print(
                    f"covered: {len(osll_keys)} OSLL protected-access "
                    f"groups occur in new locklint; {len(newll_only)} "
                    f"reviewed native-only group{suffix}"
                )
        else:
            print(
                f"equivalent: {len(osll_keys)} protected-access groups "
                "occur on both sides"
            )
    return equivalent


def location_finding_key(finding: Finding) -> LocationFindingKey:
    return LocationFindingKey(
        finding.source,
        finding.line,
        finding.operation,
        finding.datum,
        finding.lock,
    )


def compare_locations(
    osll: Iterable[Finding], newll: Iterable[Finding], expect: str
) -> bool:
    osll_counts = Counter(location_finding_key(finding) for finding in osll)
    newll_counts = Counter(location_finding_key(finding) for finding in newll)
    equivalent = True

    for key in sorted(osll_counts.keys() | newll_counts.keys()):
        required = osll_counts[key]
        observed = newll_counts[key]
        if observed < required:
            print(
                f"missing from new locklint: {describe_location(key)} "
                f"(OSLL {required}, new locklint {observed})"
            )
            equivalent = False
        elif observed > required and expect == "equivalent":
            print(
                f"unexpected in new locklint: {describe_location(key)} "
                f"(OSLL {required}, new locklint {observed})"
            )
            equivalent = False

    if equivalent:
        newll_only = len(newll_counts.keys() - osll_counts.keys())
        if expect == "osll-covered":
            suffix = "" if newll_only == 1 else "s"
            print(
                f"covered: {len(osll_counts)} OSLL protected-access "
                f"locations occur in new locklint; {newll_only} new "
                f"locklint-only location{suffix}"
            )
        else:
            print(
                f"equivalent: {len(osll_counts)} protected-access "
                "locations occur on both sides"
            )
    return equivalent


def write_normalized(
    path: Path, osll: Iterable[Finding], newll: Iterable[Finding]
) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(OSLL_REFERENCE_HEADER)
        for analyzer, findings in (("osll", osll), ("new-locklint", newll)):
            for finding in sorted(findings):
                writer.writerow(
                    [
                        analyzer,
                        finding.source,
                        finding.line,
                        "" if finding.column is None else finding.column,
                        "" if finding.function is None else finding.function,
                        finding.operation,
                        finding.datum,
                        finding.lock,
                    ]
                )


def write_osll_reference(path: Path, osll: Iterable[Finding]) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        stream.write(
            "# Normalized Old Solaris Lock Lint protected-access findings.\n"
        )
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(OSLL_REFERENCE_HEADER)
        for finding in sorted(osll):
            writer.writerow(
                [
                    "osll",
                    finding.source,
                    finding.line,
                    "",
                    finding.function,
                    finding.operation,
                    finding.datum,
                    finding.lock,
                ]
            )


def parse_native_only_reference(path: Path) -> set[FindingKey]:
    findings: set[FindingKey] = set()
    has_comment = False

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            if not line:
                has_comment = False
                continue
            if line.startswith("#"):
                has_comment = True
                continue
            if not has_comment:
                raise InputError(
                    f"{path}:{line_number}: native-only group has no "
                    "preceding rationale comment"
                )
            fields = line.split("\t")
            if len(fields) != 4 or not all(fields):
                raise InputError(
                    f"{path}:{line_number}: expected tab-separated "
                    "source, operation, datum, and lock"
                )
            source, operation, datum, lock = fields
            if operation not in ("read", "modify"):
                raise InputError(
                    f"{path}:{line_number}: invalid operation '{operation}'"
                )
            finding = FindingKey(source, operation, datum, lock)
            if finding in findings:
                raise InputError(
                    f"{path}:{line_number}: duplicate native-only group"
                )
            findings.add(finding)
    return findings


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="compare OSLL and new locklint protected-access reports"
    )
    parser.add_argument("--from-osll", required=True, type=Path)
    parser.add_argument("--from-newll", type=Path)
    parser.add_argument(
        "--match",
        choices=("semantic", "location"),
        default="semantic",
        help="comparison key (default: semantic)",
    )
    parser.add_argument(
        "--expect",
        choices=("equivalent", "osll-covered"),
        default="equivalent",
        help="required relationship between reports (default: equivalent)",
    )
    parser.add_argument(
        "--normalized-output",
        type=Path,
        help="write all parsed protected-access reports as TSV",
    )
    parser.add_argument(
        "--osll-reference-output",
        type=Path,
        help="write parsed OSLL findings as a reusable normalized reference",
    )
    parser.add_argument(
        "--native-only-reference",
        type=Path,
        help="reviewed native-only semantic groups with rationale comments",
    )
    arguments = parser.parse_args()
    if (
        arguments.from_newll is None
        and arguments.osll_reference_output is None
    ):
        parser.error(
            "--from-newll is required unless --osll-reference-output is used"
        )
    if arguments.normalized_output is not None and arguments.from_newll is None:
        parser.error("--normalized-output requires --from-newll")
    if arguments.native_only_reference is not None and (
        arguments.expect != "osll-covered"
        or arguments.match != "semantic"
        or arguments.from_newll is None
    ):
        parser.error(
            "--native-only-reference requires --expect=osll-covered "
            "and --match=semantic"
        )
    return arguments


def main() -> int:
    arguments = parse_arguments()
    try:
        osll = parse_osll(arguments.from_osll)
        if arguments.osll_reference_output is not None:
            write_osll_reference(arguments.osll_reference_output, osll)
        if arguments.from_newll is None:
            return 0
        newll = parse_newll(arguments.from_newll)
        reviewed_newll_only = (
            parse_native_only_reference(arguments.native_only_reference)
            if arguments.native_only_reference is not None
            else None
        )
        if arguments.normalized_output is not None:
            write_normalized(arguments.normalized_output, osll, newll)
    except (InputError, OSError) as error:
        print(f"compare-lock-reports: {error}", file=sys.stderr)
        return 2
    if arguments.match == "location":
        equivalent = compare_locations(osll, newll, arguments.expect)
    else:
        equivalent = compare_semantic(
            osll, newll, arguments.expect, reviewed_newll_only
        )
    return 0 if equivalent else 1


if __name__ == "__main__":
    sys.exit(main())
