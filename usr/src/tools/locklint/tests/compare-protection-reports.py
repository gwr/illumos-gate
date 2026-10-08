#!/usr/bin/env python3
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://illumos.org/license/CDDL.
#

"""
Compare observed-protection reports from OSLL and new locklint.

OSLL exposes the common held-lock intersection and marks a writable datum
whose intersection is empty.  New locklint retains more detailed reasons and
observed-lock samples.  The comparison uses only shared OSLL facts while the
normalized output preserves the native detail for review.
"""

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
import re
import sys
from typing import Iterable, Optional


@dataclass(frozen=True, order=True)
class ProtectionRecord:
    analyzer: str
    original_datum: str
    datum: str
    write_state: str
    common_state: str
    common_locks: tuple[str, ...]
    observed_locks: tuple[str, ...]
    protection: str
    notes: tuple[str, ...]


class InputError(Exception):
    pass


REFERENCE_HEADER = [
    "analyzer",
    "original_datum",
    "datum",
    "write_state",
    "common_state",
    "common_locks",
    "observed_locks",
    "protection",
    "notes",
]
OSLL_RECORD = re.compile(r"^(\S+)\t(\*)?held=\{\s*(.*?)\s*\}\s*$")
OSLL_UNOBSERVED = re.compile(r"^\S+$")
FILE_LOCAL_NAME = re.compile(r"^(.*\.[ch]):([^:]+)$")
PROTECTION = re.compile(r"^([a-z-]+(?:\+[a-z-]+)*)(?:\((note[1-6])\))?$")
LOCK_PROTECTIONS = {"mutex", "rwlock", "locks"}
VALID_PROTECTIONS = LOCK_PROTECTIONS | {"none", "unknown", "read-only"}


def normalize_name(name: str) -> str:
    match = FILE_LOCAL_NAME.match(name)
    if match is None:
        return name
    return f"{match.group(1)}::{match.group(2)}"


def parse_lock_list(
    value: str, separator: Optional[str]
) -> tuple[str, ...]:
    if not value:
        return ()
    items = (
        value.split()
        if separator is None
        else [item.strip() for item in value.split(separator)]
    )
    locks = tuple(sorted(normalize_name(item) for item in items))
    if any(not lock for lock in locks):
        raise ValueError("empty lock name")
    if len(set(locks)) != len(locks):
        raise ValueError("duplicate lock name")
    return locks


def records_by_datum(
    path: Path, records: Iterable[ProtectionRecord]
) -> dict[str, ProtectionRecord]:
    result: dict[str, ProtectionRecord] = {}
    for record in records:
        if record.datum in result:
            raise InputError(
                f"{path}: duplicate protection datum '{record.datum}'"
            )
        result[record.datum] = record
    return result


def is_osll_reference(path: Path) -> bool:
    with path.open(encoding="utf-8", errors="replace") as stream:
        for raw_line in stream:
            line = raw_line.rstrip("\n")
            if line and not line.startswith("#"):
                return line.split("\t", 1)[0] == "analyzer"
    return False


def parse_osll_raw(path: Path) -> list[ProtectionRecord]:
    records: list[ProtectionRecord] = []

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            if not line:
                continue
            match = OSLL_RECORD.match(line)
            if match is None:
                if OSLL_UNOBSERVED.match(line) is None:
                    raise InputError(
                        f"{path}:{line_number}: unrecognized OSLL protection "
                        "report"
                    )
                records.append(
                    ProtectionRecord(
                        "osll",
                        line,
                        normalize_name(line),
                        "unknown",
                        "unobserved",
                        (),
                        (),
                        "",
                        (),
                    )
                )
                continue
            original_datum, writable, held = match.groups()
            try:
                locks = parse_lock_list(held, None)
            except ValueError as error:
                raise InputError(
                    f"{path}:{line_number}: invalid OSLL held-lock set: "
                    f"{error}"
                ) from error
            if writable is not None and locks:
                raise InputError(
                    f"{path}:{line_number}: writable-empty marker has held "
                    "locks"
                )
            records.append(
                ProtectionRecord(
                    "osll",
                    original_datum,
                    normalize_name(original_datum),
                    (
                        "yes"
                        if writable is not None
                        else "unknown" if locks else "no"
                    ),
                    "locks" if locks else "empty",
                    locks,
                    locks,
                    "",
                    (),
                )
            )

    records_by_datum(path, records)
    return records


def parse_reference_locks(
    path: Path, line_number: int, value: str, field: str
) -> tuple[str, ...]:
    try:
        return parse_lock_list(value, ",")
    except ValueError as error:
        raise InputError(
            f"{path}:{line_number}: invalid {field}: {error}"
        ) from error


def parse_osll_reference(path: Path) -> list[ProtectionRecord]:
    records: list[ProtectionRecord] = []
    saw_header = False

    with path.open(encoding="utf-8", errors="replace", newline="") as stream:
        reader = csv.reader(stream, delimiter="\t")
        for line_number, fields in enumerate(reader, 1):
            if not fields or (len(fields) == 1 and not fields[0]):
                continue
            if fields[0].startswith("#"):
                continue
            if not saw_header:
                if fields != REFERENCE_HEADER:
                    raise InputError(
                        f"{path}:{line_number}: invalid OSLL reference header"
                    )
                saw_header = True
                continue
            if len(fields) != len(REFERENCE_HEADER):
                raise InputError(
                    f"{path}:{line_number}: expected "
                    f"{len(REFERENCE_HEADER)} tab-separated fields"
                )
            (
                analyzer,
                original_datum,
                datum,
                write_state,
                common_state,
                common_locks,
                observed_locks,
                protection,
                notes,
            ) = fields
            if analyzer != "osll":
                raise InputError(
                    f"{path}:{line_number}: expected analyzer 'osll'"
                )
            if not original_datum or not datum:
                raise InputError(
                    f"{path}:{line_number}: missing protection datum"
                )
            if write_state not in ("yes", "no", "unknown"):
                raise InputError(
                    f"{path}:{line_number}: invalid write state "
                    f"'{write_state}'"
                )
            if common_state not in (
                "locks",
                "empty",
                "unresolved",
                "unobserved",
            ):
                raise InputError(
                    f"{path}:{line_number}: invalid common state "
                    f"'{common_state}'"
                )
            parsed_common = parse_reference_locks(
                path, line_number, common_locks, "common locks"
            )
            parsed_observed = parse_reference_locks(
                path, line_number, observed_locks, "observed locks"
            )
            if common_state == "locks" and not parsed_common:
                raise InputError(
                    f"{path}:{line_number}: lock state has no common lock"
                )
            if common_state != "locks" and parsed_common:
                raise InputError(
                    f"{path}:{line_number}: {common_state} state has common "
                    "locks"
                )
            records.append(
                ProtectionRecord(
                    analyzer,
                    original_datum,
                    datum,
                    write_state,
                    common_state,
                    parsed_common,
                    parsed_observed,
                    protection,
                    tuple(filter(None, notes.split(","))),
                )
            )

    if not saw_header:
        raise InputError(f"{path}: no OSLL reference header found")
    records_by_datum(path, records)
    return records


def parse_osll(path: Path) -> list[ProtectionRecord]:
    if is_osll_reference(path):
        return parse_osll_reference(path)
    return parse_osll_raw(path)


def parse_native_locks(
    path: Path, line_number: int, value: str
) -> tuple[str, ...]:
    if value == "-":
        return ()
    try:
        return parse_lock_list(value, ",")
    except ValueError as error:
        raise InputError(
            f"{path}:{line_number}: invalid native lock list: {error}"
        ) from error


def parse_newll(path: Path) -> list[ProtectionRecord]:
    records: list[ProtectionRecord] = []
    saw_header = False

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line_number, raw_line in enumerate(stream, 1):
            line = raw_line.rstrip("\n")
            if not saw_header:
                if line == "DATUM\tACCESS\tPROTECTION\tLOCK":
                    saw_header = True
                continue
            if not line or line == "Notes:":
                break
            fields = line.split("\t")
            if len(fields) != 4 or not all(fields):
                raise InputError(
                    f"{path}:{line_number}: expected four tab-separated "
                    "native protection fields"
                )
            original_datum, access, rendered_protection, rendered_locks = fields
            if access not in ("read-only", "read/write"):
                raise InputError(
                    f"{path}:{line_number}: invalid native access "
                    f"'{access}'"
                )
            match = PROTECTION.match(rendered_protection)
            if match is None:
                raise InputError(
                    f"{path}:{line_number}: invalid native protection "
                    f"'{rendered_protection}'"
                )
            protection, note = match.groups()
            components = set(protection.split("+"))
            if not components or not components <= VALID_PROTECTIONS:
                raise InputError(
                    f"{path}:{line_number}: unknown native protection "
                    f"'{protection}'"
                )
            observed_locks = parse_native_locks(
                path, line_number, rendered_locks
            )
            if "none" in components and observed_locks:
                raise InputError(
                    f"{path}:{line_number}: none protection has lock names"
                )
            if components & LOCK_PROTECTIONS and not observed_locks:
                raise InputError(
                    f"{path}:{line_number}: lock protection has no lock name"
                )
            if note == "note5" or "unknown" in components:
                common_state = "unresolved"
                common_locks: tuple[str, ...] = ()
            elif note in ("note1", "note2", "note3", "note4"):
                common_state = "empty"
                common_locks = ()
            elif components & LOCK_PROTECTIONS:
                common_state = "locks"
                common_locks = observed_locks
            else:
                common_state = "empty"
                common_locks = ()
            records.append(
                ProtectionRecord(
                    "new-locklint",
                    original_datum,
                    normalize_name(original_datum),
                    "yes" if access == "read/write" else "no",
                    common_state,
                    common_locks,
                    observed_locks,
                    protection,
                    () if note is None else (note,),
                )
            )

    if not saw_header:
        raise InputError(f"{path}: no native protection report header found")
    records_by_datum(path, records)
    return records


def format_locks(locks: tuple[str, ...]) -> str:
    return ", ".join(locks) if locks else "empty"


def compare_reports(
    osll: Iterable[ProtectionRecord], newll: Iterable[ProtectionRecord]
) -> bool:
    all_osll_by_datum = {record.datum: record for record in osll}
    osll_by_datum = {
        datum: record
        for datum, record in all_osll_by_datum.items()
        if record.common_state != "unobserved"
    }
    newll_by_datum = {record.datum: record for record in newll}
    equivalent = True

    for datum in sorted(osll_by_datum.keys() - newll_by_datum.keys()):
        print(f"missing from new locklint: protection data '{datum}'")
        equivalent = False
    for datum in sorted(newll_by_datum.keys() - all_osll_by_datum.keys()):
        print(f"unexpected in new locklint: protection data '{datum}'")
        equivalent = False
    for datum in sorted(osll_by_datum.keys() & newll_by_datum.keys()):
        old = osll_by_datum[datum]
        new = newll_by_datum[datum]
        if new.common_state == "unresolved":
            print(f"new locklint protection unresolved for '{datum}'")
            equivalent = False
        elif old.common_state != new.common_state:
            print(
                f"common protection differs for '{datum}': "
                f"OSLL {format_locks(old.common_locks)}, "
                f"new locklint {format_locks(new.common_locks)}"
            )
            equivalent = False
        elif (
            old.common_state == "locks"
            and old.common_locks != new.common_locks
        ):
            print(
                f"common locks differ for '{datum}': "
                f"OSLL {format_locks(old.common_locks)}, "
                f"new locklint {format_locks(new.common_locks)}"
            )
            equivalent = False
        if old.write_state != "unknown" and (
            old.write_state != new.write_state
        ):
            print(
                f"write state differs for '{datum}': "
                f"OSLL {old.write_state}, new locklint {new.write_state}"
            )
            equivalent = False

    if equivalent:
        print(
            f"equivalent: {len(osll_by_datum)} protection records agree "
            "on shared OSLL facts"
        )
    return equivalent


def record_fields(record: ProtectionRecord) -> list[str]:
    return [
        record.analyzer,
        record.original_datum,
        record.datum,
        record.write_state,
        record.common_state,
        ",".join(record.common_locks),
        ",".join(record.observed_locks),
        record.protection,
        ",".join(record.notes),
    ]


def write_records(
    stream, records: Iterable[ProtectionRecord]
) -> None:
    writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
    writer.writerow(REFERENCE_HEADER)
    for record in sorted(records):
        writer.writerow(record_fields(record))


def write_normalized(
    path: Path,
    osll: Iterable[ProtectionRecord],
    newll: Iterable[ProtectionRecord],
) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.writer(stream, delimiter="\t", lineterminator="\n")
        writer.writerow(REFERENCE_HEADER)
        for records in (osll, newll):
            for record in sorted(records):
                writer.writerow(record_fields(record))


def write_osll_reference(
    path: Path, records: Iterable[ProtectionRecord]
) -> None:
    with path.open("w", encoding="utf-8", newline="") as stream:
        stream.write(
            "# Normalized Old Solaris Lock Lint protection report.\n"
        )
        write_records(stream, records)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="compare OSLL and new locklint protection reports"
    )
    parser.add_argument("--from-osll", required=True, type=Path)
    parser.add_argument("--from-newll", type=Path)
    parser.add_argument(
        "--normalized-output",
        type=Path,
        help="write all parsed protection records as TSV",
    )
    parser.add_argument(
        "--osll-reference-output",
        type=Path,
        help="write parsed OSLL records as a reusable normalized reference",
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
        if arguments.normalized_output is not None:
            write_normalized(arguments.normalized_output, osll, newll)
    except (InputError, OSError) as error:
        print(f"compare-protection-reports: {error}", file=sys.stderr)
        return 2
    return 0 if compare_reports(osll, newll) else 1


if __name__ == "__main__":
    sys.exit(main())
