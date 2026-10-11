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
import configparser
import csv
from dataclasses import dataclass
import io
import os
from pathlib import Path, PurePosixPath
import subprocess
import sys


GROUPS = ('core', 'platform', 'userland', 'tests', 'exclude')
SIDES = ('illumos', 'openzfs')
ALLOWED_SETTINGS = {
    'repositories': {'openzfs', 'illumos'},
    'paths': {'lists'},
}


class OzfsDiffError(Exception):
    pass


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
        '--ilc', '--illumos-commit', dest='illumos_commit', required=True,
        help='illumos commit to compare')
    parser.add_argument(
        '--out', dest='output',
        help='create detailed matching reports in this new directory')
    return parser.parse_args()


def load_config(path):
    config_path = Path(path).expanduser()
    try:
        config_path = config_path.resolve(strict=True)
    except OSError as error:
        raise OzfsDiffError(
            f'cannot read configuration file {config_path}: {error.strerror}')

    config = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with config_path.open(encoding='utf-8') as stream:
            config.read_file(stream)
    except (OSError, configparser.Error) as error:
        raise OzfsDiffError(
            f'cannot read configuration file {config_path}: {error}')

    for section in config.sections():
        if section not in ALLOWED_SETTINGS:
            raise OzfsDiffError(
                f'unknown configuration section: {section}')
        for setting in config[section]:
            if setting not in ALLOWED_SETTINGS[section]:
                raise OzfsDiffError(
                    f'unknown configuration setting: {section}.{setting}')

    if not config.has_section('repositories'):
        raise OzfsDiffError('missing configuration section: repositories')

    openzfs_value = config.get('repositories', 'openzfs', fallback='').strip()
    if not openzfs_value:
        raise OzfsDiffError(
            'missing configuration setting: repositories.openzfs')

    base = Path.cwd()
    openzfs_repo = resolve_config_path(openzfs_value, base)

    illumos_value = config.get(
        'repositories', 'illumos', fallback='').strip()
    if illumos_value:
        illumos_repo = resolve_config_path(illumos_value, base)
    else:
        illumos_repo = Path.cwd().resolve()

    lists_value = config.get('paths', 'lists', fallback='').strip()
    if lists_value:
        lists_dir = resolve_config_path(lists_value, base)
    else:
        lists_dir = Path(__file__).resolve().parent / 'zfs-paths'

    return illumos_repo, openzfs_repo, lists_dir


def resolve_config_path(value, base):
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def load_path_lists(lists_dir):
    result = {}
    for group in GROUPS:
        result[group] = {}
        for side in SIDES:
            name = f'{group}-{side}.txt'
            path = lists_dir / name
            try:
                lines = path.read_text(encoding='utf-8').splitlines()
            except FileNotFoundError:
                raise OzfsDiffError(f'missing path list: {name}')
            except OSError as error:
                raise OzfsDiffError(
                    f'cannot read path list {path}: {error.strerror}')

            paths = []
            for line_number, line in enumerate(lines, 1):
                value = line.strip()
                if not value:
                    continue
                validate_repo_path(value, name, line_number)
                paths.append(value)
            result[group][side] = paths
    return result


def validate_repo_path(value, name, line_number):
    path = PurePosixPath(value)
    if (path.is_absolute() or '..' in path.parts or value.startswith(':')):
        raise OzfsDiffError(
            f'invalid repository path in {name}:{line_number}: {value}')


def validate_repository(path, label):
    if not path.is_dir():
        raise OzfsDiffError(f'{label} repository is not a directory: {path}')

    result = run_git(path, 'rev-parse', '--show-toplevel')
    if result.returncode != 0:
        raise OzfsDiffError(
            f'{label} repository is not a Git work tree: {path}')

    return Path(result.stdout.decode().strip()).resolve()


def resolve_commit(repo, value, label):
    result = run_git(
        repo, 'rev-parse', '--verify', '--end-of-options',
        f'{value}^{{commit}}')
    if result.returncode != 0:
        raise OzfsDiffError(f'{label} commit is not a commit: {value}')
    return result.stdout.decode().strip()


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


def match_files(illumos_files, openzfs_files):
    illumos_unmatched = dict(illumos_files)
    openzfs_unmatched = dict(openzfs_files)
    matches = []

    match_unique_values(
        illumos_unmatched, openzfs_unmatched, matches,
        key=lambda item: item.basename, method='basename')
    match_unique_values(
        illumos_unmatched, openzfs_unmatched, matches,
        key=lambda item: item.oid, method='content')
    match_unique_values(
        illumos_unmatched, openzfs_unmatched, matches,
        key=lambda item: item.basename, method='basename')

    illumos_names = {
        item.basename for item in illumos_unmatched.values()
    }
    openzfs_names = {
        item.basename for item in openzfs_unmatched.values()
    }
    return MatchResult(
        matches=matches,
        ambiguous_basenames=illumos_names & openzfs_names,
        illumos_unmatched=illumos_unmatched,
        openzfs_unmatched=openzfs_unmatched,
    )


def match_unique_values(
        illumos_unmatched, openzfs_unmatched, matches, *, key, method):
    illumos_by_value = index_files(illumos_unmatched.values(), key)
    openzfs_by_value = index_files(openzfs_unmatched.values(), key)

    for value in sorted(illumos_by_value.keys() & openzfs_by_value.keys()):
        illumos_candidates = illumos_by_value[value]
        openzfs_candidates = openzfs_by_value[value]
        if len(illumos_candidates) != 1 or len(openzfs_candidates) != 1:
            continue
        illumos_file = illumos_candidates[0]
        openzfs_file = openzfs_candidates[0]
        matches.append(FileMatch(
            illumos=illumos_file,
            openzfs=openzfs_file,
            method=method,
        ))
        del illumos_unmatched[illumos_file.path]
        del openzfs_unmatched[openzfs_file.path]


def index_files(files, key):
    result = defaultdict(list)
    for item in files:
        result[key(item)].append(item)
    return result


def format_match_summary(group, result):
    methods = defaultdict(int)
    for match in result.matches:
        methods[match.method] += 1
    return (
        f'  {group}: basename={methods["basename"]} '
        f'content={methods["content"]} '
        f'ambiguous={len(result.ambiguous_basenames)} '
        f'illumos-unmatched={len(result.illumos_unmatched)} '
        f'OpenZFS-unmatched={len(result.openzfs_unmatched)}'
    )


def build_summary(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_commit,
        files, match_results):
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
    return stream.getvalue()


def write_report(path, summary, files, match_results):
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
         'openzfs-path', 'illumos-oid', 'openzfs-oid'),
        match_rows)
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


def write_tsv(path, header, rows):
    try:
        with path.open('w', encoding='utf-8', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(header)
            writer.writerows(rows)
    except OSError as error:
        raise OzfsDiffError(
            f'cannot write report file {path}: {error.strerror}')


def run_git(repo, *args):
    return subprocess.run(
        ['git', '-C', str(repo), *args],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)


def run():
    args = parse_args()
    illumos_path, openzfs_path, lists_dir = load_config(args.config)
    path_lists = load_path_lists(lists_dir)

    illumos_repo = validate_repository(illumos_path, 'illumos')
    openzfs_repo = validate_repository(openzfs_path, 'OpenZFS')
    illumos_commit = resolve_commit(
        illumos_repo, args.illumos_commit, 'illumos')
    openzfs_commit = resolve_commit(
        openzfs_repo, args.openzfs_commit, 'OpenZFS')

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

    core_openzfs = dict(files['core']['openzfs'])
    core_openzfs.update(files['platform']['openzfs'])
    match_results = {
        'core': match_files(files['core']['illumos'], core_openzfs),
        'userland': match_files(
            files['userland']['illumos'], files['userland']['openzfs']),
        'tests': match_files(
            files['tests']['illumos'], files['tests']['openzfs']),
    }

    summary = build_summary(
        illumos_repo, illumos_commit, openzfs_repo, openzfs_commit,
        files, match_results)
    if args.output:
        write_report(args.output, summary, files, match_results)
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
