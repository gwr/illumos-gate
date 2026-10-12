#!/usr/bin/python3
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

#
# Copyright 2026 RackTop Systems Inc.
#

import argparse
from collections import defaultdict
import csv
from dataclasses import dataclass
import io
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile

from ozfs_common import (
    GROUPS,
    SIDES,
    OzfsError as OzfsDiffError,
    changed_paths,
    classify_path,
    load_config,
    load_path_lists,
    load_platform_mappings,
    path_matches_any,
    resolve_commit,
    run_git,
    validate_repository,
)


@dataclass(frozen=True)
class FileInfo:
    path: str
    basename: str
    oid: str
    size: int


@dataclass(frozen=True)
class FileMatch:
    illumos: FileInfo
    openzfs: FileInfo
    method: str


@dataclass
class MatchResult:
    matches: list
    ambiguous_basenames: set
    illumos_unmatched: dict
    openzfs_unmatched: dict


@dataclass(frozen=True)
class FileComparison:
    group: str
    match: FileMatch
    kind: str
    exceptional: bool
    added: int
    deleted: int


@dataclass(frozen=True)
class ScopedComparison:
    group: str
    source: str
    target: str
    exceptional: bool
    before_kind: str
    before_added: int
    before_deleted: int
    after_kind: str
    after_added: int
    after_deleted: int


def parse_args():
    parser = argparse.ArgumentParser(
        description='Compare ZFS source trees from illumos and OpenZFS')
    parser.add_argument(
        '--cf', '--config', dest='config', required=True,
        help='configuration file')
    parser.add_argument(
        '--ozc', '--openzfs-commit', dest='openzfs_commit', required=True,
        help='OpenZFS commit to compare')
    parser.add_argument(
        '--ozb', '--openzfs-base', dest='openzfs_base',
        help='limit comparison to paths changed after this OpenZFS commit')
    parser.add_argument(
        '--ilc', '--illumos-commit', dest='illumos_commit', required=True,
        help='illumos commit to compare')
    parser.add_argument(
        '--out', dest='output',
        help='create detailed matching reports in this new directory')
    return parser.parse_args()


def enumerate_files(repo, commit, paths):
    if not paths:
        return {}

    result = run_git(
        repo, 'ls-tree', '-r', '-l', '-z', commit, '--', *paths)
    if result.returncode != 0:
        message = result.stderr.decode(errors='replace').strip()
        raise OzfsDiffError(f'cannot enumerate files at {commit}: {message}')

    files = {}
    for record in result.stdout.split(b'\0'):
        if not record:
            continue
        metadata, encoded_path = record.split(b'\t', 1)
        _mode, object_type, encoded_oid, encoded_size = metadata.split()
        if object_type != b'blob':
            continue
        path = os.fsdecode(encoded_path)
        files[path] = FileInfo(
            path=path,
            basename=PurePosixPath(path).name,
            oid=encoded_oid.decode(),
            size=int(encoded_size),
        )
    return files


def match_path_mappings(illumos_files, openzfs_files, mappings):
    illumos_unmatched = dict(illumos_files)
    openzfs_unmatched = dict(openzfs_files)
    matches = []

    for illumos_root, openzfs_root in mappings:
        match_path_mapping(
            illumos_unmatched, openzfs_unmatched, matches,
            illumos_root, openzfs_root)

    return MatchResult(
        matches=matches,
        ambiguous_basenames=set(),
        illumos_unmatched=illumos_unmatched,
        openzfs_unmatched=openzfs_unmatched,
    )


def match_path_mapping(
        illumos_unmatched, openzfs_unmatched, matches,
        illumos_root, openzfs_root):
    illumos_candidates = files_under_root(illumos_unmatched, illumos_root)
    openzfs_candidates = files_under_root(openzfs_unmatched, openzfs_root)

    if (illumos_root in illumos_candidates and
            openzfs_root in openzfs_candidates):
        add_path_match(
            illumos_unmatched, openzfs_unmatched, matches,
            illumos_candidates[illumos_root],
            openzfs_candidates[openzfs_root])
        return

    illumos_by_relative = {
        relative_path(illumos_root, item.path): item
        for item in illumos_candidates.values()
    }
    openzfs_by_relative = {
        relative_path(openzfs_root, item.path): item
        for item in openzfs_candidates.values()
    }
    for relative in sorted(
            illumos_by_relative.keys() & openzfs_by_relative.keys()):
        add_path_match(
            illumos_unmatched, openzfs_unmatched, matches,
            illumos_by_relative[relative],
            openzfs_by_relative[relative])


def files_under_root(files, root):
    prefix = f'{root}/'
    return {
        path: item for path, item in files.items()
        if path == root or path.startswith(prefix)
    }


def relative_path(root, path):
    if path == root:
        return PurePosixPath(path).name
    return path[len(root) + 1:]


def add_path_match(
        illumos_unmatched, openzfs_unmatched, matches,
        illumos_file, openzfs_file):
    matches.append(FileMatch(
        illumos=illumos_file,
        openzfs=openzfs_file,
        method='path',
    ))
    del illumos_unmatched[illumos_file.path]
    del openzfs_unmatched[openzfs_file.path]


def format_match_summary(group, result):
    return (
        f'  {group}: path={len(result.matches)} '
        f'ambiguous={len(result.ambiguous_basenames)} '
        f'illumos-unmatched={len(result.illumos_unmatched)} '
        f'OpenZFS-unmatched={len(result.openzfs_unmatched)}'
    )


def build_summary(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_commit,
        files, match_results, comparisons):
    stream = io.StringIO()
    print(f'illumos repository: {illumos_repo}', file=stream)
    print(f'illumos commit: {illumos_commit}', file=stream)
    print(f'OpenZFS repository: {openzfs_repo}', file=stream)
    print(f'OpenZFS commit: {openzfs_commit}', file=stream)
    print('groups:', file=stream)
    for group in GROUPS:
        illumos_count = len(files[group]['illumos'])
        openzfs_count = len(files[group]['openzfs'])
        print(
            f'  {group}: illumos={illumos_count} OpenZFS={openzfs_count}',
            file=stream)
    print('matching:', file=stream)
    for group in ('core', 'userland', 'tests'):
        print(format_match_summary(group, match_results[group]), file=stream)

    platform_paths = set(files['platform']['openzfs'])
    platform_matched = sum(
        match.openzfs.path in platform_paths
        for match in match_results['core'].matches)
    platform_unmatched = sum(
        path in platform_paths
        for path in match_results['core'].openzfs_unmatched)
    print(
        f'  platform supplement: matched={platform_matched} '
        f'unmatched={platform_unmatched}',
        file=stream)
    print('differences:', file=stream)
    for group in ('core', 'userland', 'tests'):
        group_comparisons = [
            comparison for comparison in comparisons
            if comparison.group == group
        ]
        included = [
            comparison for comparison in group_comparisons
            if not comparison.exceptional
        ]
        kinds = defaultdict(int)
        for comparison in included:
            kinds[comparison.kind] += 1
        exceptional_count = sum(
            item.exceptional for item in group_comparisons)
        print(
            f'  {group}: identical={kinds["identical"]} '
            f'text={kinds["text"]} binary={kinds["binary"]} '
            f'exceptional={exceptional_count} '
            f'added={sum(item.added for item in included)} '
            f'deleted={sum(item.deleted for item in included)}',
            file=stream)
    return stream.getvalue()


def write_report(path, summary, files, match_results, comparisons):
    output = Path(path).expanduser()
    if not output.is_absolute():
        output = Path.cwd() / output
    output = output.resolve()
    try:
        output.mkdir(parents=True)
    except FileExistsError:
        raise OzfsDiffError(f'output path already exists: {output}')
    except OSError as error:
        raise OzfsDiffError(
            f'cannot create output directory {output}: {error.strerror}')

    platform_paths = set(files['platform']['openzfs'])
    match_rows = []
    ambiguous_rows = []
    illumos_unmatched_rows = []
    openzfs_unmatched_rows = []

    for group in ('core', 'userland', 'tests'):
        result = match_results[group]
        for match in sorted(
                result.matches,
                key=lambda item: (item.illumos.path, item.openzfs.path)):
            source = openzfs_source(group, match.openzfs.path, platform_paths)
            match_rows.append((
                group,
                match.method,
                source,
                match.illumos.path,
                match.openzfs.path,
                match.illumos.oid,
                match.openzfs.oid,
            ))

        for basename in sorted(result.ambiguous_basenames):
            for side, unmatched in (
                    ('illumos', result.illumos_unmatched),
                    ('OpenZFS', result.openzfs_unmatched)):
                candidates = (
                    item for item in unmatched.values()
                    if item.basename == basename)
                for item in sorted(candidates, key=lambda entry: entry.path):
                    ambiguous_rows.append((
                        group, basename, side, item.path, item.oid))

        for item in sorted(
                result.illumos_unmatched.values(),
                key=lambda entry: entry.path):
            illumos_unmatched_rows.append((group, item.path))
        for item in sorted(
                result.openzfs_unmatched.values(),
                key=lambda entry: entry.path):
            source = openzfs_source(group, item.path, platform_paths)
            openzfs_unmatched_rows.append((group, source, item.path))

    write_tsv(
        output / 'matches.tsv',
        ('group', 'method', 'openzfs-source', 'illumos-path',
         'openzfs-path', 'illumos-oid', 'openzfs-oid', 'exceptional'),
        add_exception_flags(match_rows, comparisons))
    write_tsv(
        output / 'comparisons.tsv',
        ('group', 'kind', 'exceptional', 'added', 'deleted',
         'illumos-path', 'openzfs-path'),
        comparison_rows(comparisons))
    write_tsv(
        output / 'ambiguous.tsv',
        ('group', 'basename', 'side', 'path', 'oid'),
        ambiguous_rows)
    write_tsv(
        output / 'unmatched-illumos.txt',
        ('group', 'path'),
        illumos_unmatched_rows)
    write_tsv(
        output / 'unmatched-openzfs.txt',
        ('group', 'source', 'path'),
        openzfs_unmatched_rows)
    try:
        (output / 'summary.txt').write_text(summary, encoding='utf-8')
    except OSError as error:
        raise OzfsDiffError(
            f'cannot write report in {output}: {error.strerror}')


def openzfs_source(group, path, platform_paths):
    if group == 'core' and path in platform_paths:
        return 'platform'
    return group


def add_exception_flags(match_rows, comparisons):
    exceptional_pairs = {
        (comparison.match.illumos.path, comparison.match.openzfs.path)
        for comparison in comparisons
        if comparison.exceptional
    }
    return [
        row + (
            boolean_text((row[3], row[4]) in exceptional_pairs),
        )
        for row in match_rows
    ]


def comparison_rows(comparisons):
    return [
        (
            comparison.group,
            comparison.kind,
            boolean_text(comparison.exceptional),
            comparison.added,
            comparison.deleted,
            comparison.match.illumos.path,
            comparison.match.openzfs.path,
        )
        for comparison in sorted(
            comparisons,
            key=lambda item: (
                item.group,
                item.match.illumos.path,
                item.match.openzfs.path,
            ))
    ]


def boolean_text(value):
    return 'true' if value else 'false'


def write_tsv(path, header, rows):
    try:
        with path.open('w', encoding='utf-8', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(header)
            writer.writerows(rows)
    except OSError as error:
        raise OzfsDiffError(
            f'cannot write report file {path}: {error.strerror}')


def compare_matches(
        illumos_repo, openzfs_repo, match_results, exception_paths):
    matches = [
        (group, match)
        for group, result in match_results.items()
        for match in result.matches
    ]
    illumos_blobs = read_blobs(
        illumos_repo, {match.illumos.oid for _group, match in matches})
    openzfs_blobs = read_blobs(
        openzfs_repo, {match.openzfs.oid for _group, match in matches})
    pairs = [
        (
            illumos_blobs[match.illumos.oid],
            openzfs_blobs[match.openzfs.oid],
        )
        for _group, match in matches
    ]
    results = compare_data_pairs(pairs)

    comparisons = []
    for index, (group, match) in enumerate(matches):
        exceptional = (
            match.illumos.path in exception_paths['illumos'] or
            match.openzfs.path in exception_paths['openzfs']
        )
        kind, added, deleted = results[index]
        comparisons.append(FileComparison(
            group=group,
            match=match,
            kind=kind,
            exceptional=exceptional,
            added=added,
            deleted=deleted,
        ))
    return comparisons


def read_blobs(repo, object_ids):
    requested = sorted(object_ids)
    if not requested:
        return {}
    result = subprocess.run(
        ['git', '-C', str(repo), 'cat-file', '--batch'],
        input=(''.join(f'{oid}\n' for oid in requested)).encode(),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        message = result.stderr.decode(errors='replace').strip()
        raise OzfsDiffError(f'cannot read Git blobs: {message}')

    stream = io.BytesIO(result.stdout)
    blobs = {}
    for requested_oid in requested:
        header = stream.readline().rstrip(b'\n').split()
        if len(header) != 3 or header[1] != b'blob':
            raise OzfsDiffError(
                f'unexpected git cat-file response for {requested_oid}')
        size = int(header[2])
        data = stream.read(size)
        if len(data) != size or stream.read(1) != b'\n':
            raise OzfsDiffError(
                f'truncated git cat-file response for {requested_oid}')
        blobs[requested_oid] = data
    return blobs


def compare_data_pairs(pairs):
    results = {
        index: ('identical', 0, 0)
        for index in range(len(pairs))
    }
    with tempfile.TemporaryDirectory(prefix='ozfs-diff-') as temporary:
        root = Path(temporary)
        left = root / 'illumos'
        right = root / 'openzfs'
        left.mkdir()
        right.mkdir()
        for index, (left_data, right_data) in enumerate(pairs):
            name = f'{index:08d}'
            if left_data is not None:
                (left / name).write_bytes(left_data)
            if right_data is not None:
                (right / name).write_bytes(right_data)

        result = subprocess.run(
            [
                'git', 'diff', '--no-index', '--no-renames',
                '--numstat', '-z', '--', str(left), str(right),
            ],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode not in (0, 1):
            message = result.stderr.decode(errors='replace').strip()
            raise OzfsDiffError(f'Git comparison failed: {message}')

        records = result.stdout.split(b'\0')
        if records and not records[-1]:
            records.pop()
        position = 0
        while position < len(records):
            fields = records[position].split(b'\t', 2)
            position += 1
            if len(fields) != 3:
                raise OzfsDiffError('unexpected Git numstat output')
            added_text, deleted_text, encoded_path = fields
            if not encoded_path:
                if position + 1 >= len(records):
                    raise OzfsDiffError('truncated Git numstat path pair')
                left_path = records[position]
                encoded_path = records[position + 1]
                position += 2
                if encoded_path == b'/dev/null':
                    encoded_path = left_path
            name = PurePosixPath(
                os.fsdecode(encoded_path)).name
            try:
                index = int(name)
            except ValueError:
                raise OzfsDiffError(
                    f'unexpected Git comparison path: '
                    f'{os.fsdecode(encoded_path)}')
            if added_text == b'-' and deleted_text == b'-':
                results[index] = ('binary', 0, 0)
            else:
                results[index] = (
                    'text', int(added_text), int(deleted_text))
    return results


def read_path(repo, commit, path):
    spec = f'{commit}:{path}'
    exists = run_git(repo, 'cat-file', '-e', spec)
    if exists.returncode != 0:
        return None
    result = run_git(repo, 'cat-file', 'blob', spec)
    if result.returncode != 0:
        message = result.stderr.decode(errors='replace').strip()
        raise OzfsDiffError(f'cannot read {spec}: {message}')
    return result.stdout


def scoped_comparisons(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_base,
        openzfs_commit, path_lists, platform_mappings):
    paths = changed_paths(openzfs_repo, openzfs_base, openzfs_commit)
    actions = [
        classify_path(
            illumos_repo, illumos_commit, path, path_lists,
            platform_mappings, reject_exception=False)
        for path in paths
    ]
    mapped = [action for action in actions if action.kind == 'mapped']

    before_pairs = []
    after_pairs = []
    for action in mapped:
        illumos_data = read_path(
            illumos_repo, illumos_commit, action.target)
        before_pairs.append((
            illumos_data,
            read_path(openzfs_repo, openzfs_base, action.source),
        ))
        after_pairs.append((
            illumos_data,
            read_path(openzfs_repo, openzfs_commit, action.source),
        ))
    before_results = compare_data_pairs(before_pairs)
    after_results = compare_data_pairs(after_pairs)

    comparisons = []
    for index, action in enumerate(mapped):
        before_kind, before_added, before_deleted = before_results[index]
        after_kind, after_added, after_deleted = after_results[index]
        exceptional = (
            path_matches_any(
                action.source, path_lists['exception']['openzfs']) or
            path_matches_any(
                action.target, path_lists['exception']['illumos']))
        comparisons.append(ScopedComparison(
            group=action.group,
            source=action.source,
            target=action.target,
            exceptional=exceptional,
            before_kind=before_kind,
            before_added=before_added,
            before_deleted=before_deleted,
            after_kind=after_kind,
            after_added=after_added,
            after_deleted=after_deleted,
        ))
    return actions, comparisons


def scoped_totals(comparisons, prefix):
    included = [
        item for item in comparisons
        if not item.exceptional
    ]
    kinds = defaultdict(int)
    for item in included:
        kinds[getattr(item, f'{prefix}_kind')] += 1
    return (
        kinds['identical'],
        kinds['text'],
        kinds['binary'],
        sum(getattr(item, f'{prefix}_added') for item in included),
        sum(getattr(item, f'{prefix}_deleted') for item in included),
    )


def format_scoped_summary(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_base,
        openzfs_commit, actions, comparisons):
    counts = defaultdict(int)
    for action in actions:
        counts[action.kind] += 1
    before = scoped_totals(comparisons, 'before')
    after = scoped_totals(comparisons, 'after')
    before_distance = before[3] + before[4]
    after_distance = after[3] + after[4]

    stream = io.StringIO()
    print(f'illumos repository: {illumos_repo}', file=stream)
    print(f'illumos commit: {illumos_commit}', file=stream)
    print(f'OpenZFS repository: {openzfs_repo}', file=stream)
    print(f'OpenZFS base: {openzfs_base}', file=stream)
    print(f'OpenZFS commit: {openzfs_commit}', file=stream)
    print(
        f'scope: changed={len(actions)} mapped={counts["mapped"]} '
        f'alternate={counts["alternate"]} excluded={counts["excluded"]}',
        file=stream)
    print(
        f'before: identical={before[0]} text={before[1]} '
        f'binary={before[2]} added={before[3]} deleted={before[4]}',
        file=stream)
    print(
        f'after: identical={after[0]} text={after[1]} '
        f'binary={after[2]} added={after[3]} deleted={after[4]}',
        file=stream)
    print(
        f'line distance: before={before_distance} after={after_distance} '
        f'delta={after_distance - before_distance:+d}',
        file=stream)
    return stream.getvalue()


def write_scoped_report(path, summary, actions, comparisons):
    output = Path(path).expanduser()
    if not output.is_absolute():
        output = Path.cwd() / output
    output = output.resolve()
    try:
        output.mkdir(parents=True)
    except FileExistsError:
        raise OzfsDiffError(f'output path already exists: {output}')
    except OSError as error:
        raise OzfsDiffError(
            f'cannot create output directory {output}: {error.strerror}')

    write_tsv(
        output / 'scope.tsv',
        ('status', 'group', 'openzfs-path', 'illumos-path'),
        (
            (action.kind, action.group, action.source, action.target)
            for action in actions
        ))
    write_tsv(
        output / 'comparisons.tsv',
        (
            'group', 'before-kind', 'after-kind', 'exceptional',
            'before-added', 'before-deleted', 'after-added',
            'after-deleted', 'illumos-path', 'openzfs-path',
        ),
        (
            (
                item.group, item.before_kind, item.after_kind,
                boolean_text(item.exceptional),
                item.before_added, item.before_deleted,
                item.after_added, item.after_deleted,
                item.target, item.source,
            )
            for item in comparisons
        ))
    try:
        (output / 'summary.txt').write_text(summary, encoding='utf-8')
    except OSError as error:
        raise OzfsDiffError(
            f'cannot write report in {output}: {error.strerror}')


def run():
    args = parse_args()
    illumos_path, openzfs_path, lists_dir = load_config(args.config)
    path_lists = load_path_lists(lists_dir)
    platform_mappings = load_platform_mappings(lists_dir)

    illumos_repo = validate_repository(illumos_path, 'illumos')
    openzfs_repo = validate_repository(openzfs_path, 'OpenZFS')
    illumos_commit = resolve_commit(
        illumos_repo, args.illumos_commit, 'illumos')
    openzfs_commit = resolve_commit(
        openzfs_repo, args.openzfs_commit, 'OpenZFS')
    if args.openzfs_base:
        openzfs_base = resolve_commit(
            openzfs_repo, args.openzfs_base, 'OpenZFS base')
        actions, comparisons = scoped_comparisons(
            illumos_repo, illumos_commit,
            openzfs_repo, openzfs_base, openzfs_commit,
            path_lists, platform_mappings)
        summary = format_scoped_summary(
            illumos_repo, illumos_commit,
            openzfs_repo, openzfs_base, openzfs_commit,
            actions, comparisons)
        if args.output:
            write_scoped_report(
                args.output, summary, actions, comparisons)
        print(summary, end='')
        return

    files = {}
    for group in GROUPS:
        illumos_files = enumerate_files(
            illumos_repo, illumos_commit, path_lists[group]['illumos'])
        openzfs_files = enumerate_files(
            openzfs_repo, openzfs_commit, path_lists[group]['openzfs'])
        files[group] = {
            'illumos': illumos_files,
            'openzfs': openzfs_files,
        }

    core_illumos = dict(files['core']['illumos'])
    core_illumos.update(files['platform']['illumos'])
    core_openzfs = dict(files['core']['openzfs'])
    core_openzfs.update(files['platform']['openzfs'])
    core_mappings = list(zip(
        path_lists['core']['illumos'],
        path_lists['core']['openzfs']))
    core_mappings.extend(zip(
        path_lists['platform']['illumos'],
        path_lists['platform']['openzfs']))
    match_results = {
        'core': match_path_mappings(
            core_illumos, core_openzfs, core_mappings),
        'userland': match_path_mappings(
            files['userland']['illumos'],
            files['userland']['openzfs'],
            zip(
                path_lists['userland']['illumos'],
                path_lists['userland']['openzfs'])),
        'tests': match_path_mappings(
            files['tests']['illumos'],
            files['tests']['openzfs'],
            zip(
                path_lists['tests']['illumos'],
                path_lists['tests']['openzfs'])),
    }
    exception_paths = {
        side: set(path_lists['exception'][side])
        for side in SIDES
    }
    comparisons = compare_matches(
        illumos_repo, openzfs_repo, match_results, exception_paths)

    summary = build_summary(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_commit,
        files, match_results, comparisons)
    if args.output:
        write_report(
            args.output, summary, files, match_results, comparisons)
    print(summary, end='')


def main():
    try:
        run()
    except OzfsDiffError as error:
        print(f'ozfs-diff: error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
