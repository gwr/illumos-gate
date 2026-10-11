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
import configparser
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
        return set()

    result = run_git(
        repo, 'ls-tree', '-r', '--name-only', '-z', commit, '--', *paths)
    if result.returncode != 0:
        message = result.stderr.decode(errors='replace').strip()
        raise OzfsDiffError(f'cannot enumerate files at {commit}: {message}')

    return {
        os.fsdecode(name)
        for name in result.stdout.split(b'\0')
        if name
    }


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

    counts = {}
    for group in GROUPS:
        illumos_files = enumerate_files(
            illumos_repo, illumos_commit, path_lists[group]['illumos'])
        openzfs_files = enumerate_files(
            openzfs_repo, openzfs_commit, path_lists[group]['openzfs'])
        counts[group] = (len(illumos_files), len(openzfs_files))

    print(f'illumos repository: {illumos_repo}')
    print(f'illumos commit: {illumos_commit}')
    print(f'OpenZFS repository: {openzfs_repo}')
    print(f'OpenZFS commit: {openzfs_commit}')
    print('groups:')
    for group in GROUPS:
        illumos_count, openzfs_count = counts[group]
        print(
            f'  {group}: illumos={illumos_count} OpenZFS={openzfs_count}')


def main():
    try:
        run()
    except OzfsDiffError as error:
        print(f'ozfs-diff: error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
