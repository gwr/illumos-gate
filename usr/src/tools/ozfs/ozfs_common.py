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

import configparser
import csv
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import subprocess


COMPARE_GROUPS = ('core', 'platform', 'userland', 'tests')
GROUPS = COMPARE_GROUPS + ('exclude',)
MODIFIERS = ('exception',)
SIDES = ('illumos', 'openzfs')
ALLOWED_SETTINGS = {
    'repositories': {'openzfs', 'illumos'},
    'paths': {'lists'},
}


class OzfsError(Exception):
    pass


@dataclass(frozen=True)
class PlatformMapping:
    role: str
    variant: str
    illumos_path: str
    openzfs_path: str


def load_config(path):
    config_path = Path(path).expanduser()
    try:
        config_path = config_path.resolve(strict=True)
    except OSError as error:
        raise OzfsError(
            f'cannot read configuration file {config_path}: {error.strerror}')

    config = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with config_path.open(encoding='utf-8') as stream:
            config.read_file(stream)
    except (OSError, configparser.Error) as error:
        raise OzfsError(
            f'cannot read configuration file {config_path}: {error}')

    for section in config.sections():
        if section not in ALLOWED_SETTINGS:
            raise OzfsError(f'unknown configuration section: {section}')
        for setting in config[section]:
            if setting not in ALLOWED_SETTINGS[section]:
                raise OzfsError(
                    f'unknown configuration setting: {section}.{setting}')

    if not config.has_section('repositories'):
        raise OzfsError('missing configuration section: repositories')

    openzfs_value = config.get('repositories', 'openzfs', fallback='').strip()
    if not openzfs_value:
        raise OzfsError(
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
    for group in GROUPS + MODIFIERS:
        result[group] = {}
        for side in SIDES:
            name = f'{group}-{side}.txt'
            path = lists_dir / name
            try:
                lines = path.read_text(encoding='utf-8').splitlines()
            except FileNotFoundError:
                raise OzfsError(f'missing path list: {name}')
            except OSError as error:
                raise OzfsError(
                    f'cannot read path list {path}: {error.strerror}')

            paths = []
            for line_number, line in enumerate(lines, 1):
                value = line.strip()
                if not value:
                    continue
                validate_repo_path(value, name, line_number)
                paths.append(value)
            result[group][side] = paths

    for group in COMPARE_GROUPS:
        illumos_name = f'{group}-illumos.txt'
        openzfs_name = f'{group}-openzfs.txt'
        illumos_count = len(result[group]['illumos'])
        openzfs_count = len(result[group]['openzfs'])
        if illumos_count != openzfs_count:
            raise OzfsError(
                f'path map length mismatch: {illumos_name} has '
                f'{illumos_count} entries; {openzfs_name} has '
                f'{openzfs_count}')
    return result


def load_platform_mappings(lists_dir):
    name = 'platform-mappings.tsv'
    path = lists_dir / name
    expected = ('role', 'variant', 'illumos-path', 'openzfs-path')
    try:
        with path.open(encoding='utf-8', newline='') as stream:
            reader = csv.DictReader(stream, delimiter='\t')
            if tuple(reader.fieldnames or ()) != expected:
                raise OzfsError(
                    f'invalid header in {name}: expected '
                    f'{" ".join(expected)}')
            rows = list(reader)
    except FileNotFoundError:
        raise OzfsError(f'missing path map: {name}')
    except OSError as error:
        raise OzfsError(f'cannot read path map {path}: {error.strerror}')

    mappings = []
    sources = set()
    primary_targets = set()
    for line_number, row in enumerate(rows, 2):
        if None in row or any(not row[field] for field in expected):
            raise OzfsError(f'invalid row in {name}:{line_number}')
        role = row['role']
        variant = row['variant']
        illumos_path = row['illumos-path']
        openzfs_path = row['openzfs-path']
        if role not in ('primary', 'alternate'):
            raise OzfsError(
                f'invalid role in {name}:{line_number}: {role}')
        if variant not in ('freebsd', 'linux'):
            raise OzfsError(
                f'invalid variant in {name}:{line_number}: {variant}')
        validate_repo_path(illumos_path, name, line_number)
        validate_repo_path(openzfs_path, name, line_number)
        if openzfs_path in sources:
            raise OzfsError(
                f'duplicate OpenZFS path in {name}:{line_number}: '
                f'{openzfs_path}')
        sources.add(openzfs_path)
        if role == 'primary':
            if illumos_path in primary_targets:
                raise OzfsError(
                    f'duplicate primary illumos path in '
                    f'{name}:{line_number}: {illumos_path}')
            primary_targets.add(illumos_path)
        mappings.append(PlatformMapping(
            role, variant, illumos_path, openzfs_path))

    for mapping in mappings:
        if (mapping.role == 'alternate' and
                mapping.illumos_path not in primary_targets):
            raise OzfsError(
                f'alternate has no primary mapping in {name}: '
                f'{mapping.illumos_path}')
    return mappings


def validate_repo_path(value, name, line_number):
    path = PurePosixPath(value)
    if path.is_absolute() or '..' in path.parts or value.startswith(':'):
        raise OzfsError(
            f'invalid repository path in {name}:{line_number}: {value}')


def validate_repository(path, label):
    if not path.is_dir():
        raise OzfsError(f'{label} repository is not a directory: {path}')

    result = run_git(path, 'rev-parse', '--show-toplevel')
    if result.returncode != 0:
        raise OzfsError(
            f'{label} repository is not a Git work tree: {path}')

    return Path(result.stdout.decode().strip()).resolve()


def resolve_commit(repo, value, label):
    result = run_git(
        repo, 'rev-parse', '--verify', '--end-of-options',
        f'{value}^{{commit}}')
    if result.returncode != 0:
        raise OzfsError(f'{label} commit is not a commit: {value}')
    return result.stdout.decode().strip()


def path_is_under(path, root):
    return path == root or path.startswith(f'{root}/')


def map_relative_path(path, source_root, target_root):
    if path == source_root:
        return target_root
    return f'{target_root}/{path[len(source_root) + 1:]}'


def run_git(repo, *args, input_data=None):
    return subprocess.run(
        ['git', '-C', str(repo), *args],
        input=input_data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False)
