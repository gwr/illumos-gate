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

import argparse
from dataclasses import dataclass
from email.utils import formataddr, formatdate
from pathlib import Path
import sys

from ozfs_common import (
    OzfsError,
    PathAction,
    changed_paths,
    classify_path,
    git_output,
    load_config,
    load_path_lists,
    load_platform_mappings,
    resolve_commit,
    validate_repository,
)


@dataclass(frozen=True)
class CommitPlan:
    commit: str
    parent: str
    subject: str
    message: str
    actions: tuple


def parse_args():
    parser = argparse.ArgumentParser(
        description='Reshape OpenZFS commits into illumos mail patches')
    parser.add_argument(
        '--cf', '--config', dest='config', required=True,
        help='configuration file')
    parser.add_argument(
        '--check', action='store_true',
        help='validate and report path handling without writing patches')
    parser.add_argument(
        '--output', type=Path,
        help='write the combined reshaped mail patch to this new file')
    parser.add_argument(
        '--work-name',
        help='work-item name used in the combined patch subject')
    parser.add_argument(
        'commits', metavar='commit', nargs='+',
        help='OpenZFS commit, in application order')
    return parser.parse_args()


def commit_parent(repo, commit):
    fields = git_output(
        repo, 'rev-list', '--parents', '-n', '1', commit).decode().split()
    if len(fields) == 1:
        raise OzfsError(f'root commit is not supported: {commit}')
    if len(fields) > 2:
        raise OzfsError(
            f'merge commit requires mainline selection: {commit}')
    return fields[1]


def plan_commit(
        illumos_repo, illumos_commit, openzfs_repo, commit, path_lists,
        platform_mappings):
    parent = commit_parent(openzfs_repo, commit)
    paths = changed_paths(openzfs_repo, parent, commit)
    actions = tuple(
        classify_path(
            illumos_repo, illumos_commit, path, path_lists,
            platform_mappings)
        for path in paths
    )
    subject = git_output(
        openzfs_repo, 'show', '-s', '--format=%s', commit
    ).decode().rstrip('\n')
    message = git_output(
        openzfs_repo, 'show', '-s', '--format=%B', commit
    ).decode().rstrip('\n')
    return CommitPlan(commit, parent, subject, message, actions)


def format_manifest(plans):
    lines = []
    for plan in plans:
        lines.append(f'commit\t{plan.commit}\t{plan.subject}')
        for action in plan.actions:
            fields = [action.kind, action.source]
            if action.target:
                fields.append(action.target)
            lines.append('\t'.join(fields))
    return '\n'.join(lines) + '\n'


def rewrite_patch_paths(patch, mappings):
    result = patch
    for source, target in mappings:
        source_bytes = source.encode()
        target_bytes = target.encode()
        replacements = (
            (
                b'diff --git a/' + source_bytes + b' b/' +
                source_bytes + b'\n',
                b'diff --git a/' + target_bytes + b' b/' +
                target_bytes + b'\n',
            ),
            (
                b'--- a/' + source_bytes + b'\n',
                b'--- a/' + target_bytes + b'\n',
            ),
            (
                b'+++ b/' + source_bytes + b'\n',
                b'+++ b/' + target_bytes + b'\n',
            ),
        )
        for old, new in replacements:
            result = result.replace(old, new)
    return result


def format_diff(openzfs_repo, plan):
    mappings = [
        (action.source, action.target)
        for action in plan.actions
        if action.kind == 'mapped'
    ]
    if not mappings:
        raise OzfsError(
            f'OpenZFS commit has no mapped illumos paths: {plan.commit}')

    patch = git_output(
        openzfs_repo, 'diff', '--full-index', '--binary', '--no-renames',
        plan.parent, plan.commit, '--',
        *(source for source, _target in mappings))
    return rewrite_patch_paths(patch, mappings)


def repository_identity(repo):
    name = git_output(repo, 'config', '--get', 'user.name').decode().strip()
    email = git_output(repo, 'config', '--get', 'user.email').decode().strip()
    if not name:
        raise OzfsError('illumos repository has no configured user.name')
    if not email:
        raise OzfsError('illumos repository has no configured user.email')
    return name, email


def quote_mbox_from_lines(text):
    return '\n'.join(
        f'>{line}' if line.startswith('From ') else line
        for line in text.split('\n')
    )


def format_combined_patch(illumos_repo, work_name, plans, diffs):
    name, email = repository_identity(illumos_repo)
    body = []
    for plan in plans:
        body.extend((
            f'import OpenZFS {plan.commit}',
            '',
            quote_mbox_from_lines(plan.message),
            '',
        ))

    headers = (
        f'From {plans[0].commit} Mon Sep 17 00:00:00 2001\n'
        f'From: {formataddr((name, email))}\n'
        f'Date: {formatdate(localtime=True)}\n'
        f'Subject: [PATCH] OpenZFS {work_name} import\n'
        '\n'
    )
    message = headers + '\n'.join(body) + '---\n'
    return message.encode() + b''.join(diffs)


def write_patch(path, patch):
    output = path.expanduser()
    if not output.is_absolute():
        output = Path.cwd() / output
    output = output.resolve()
    try:
        with output.open('xb') as stream:
            stream.write(patch)
    except FileExistsError:
        raise OzfsError(f'output path already exists: {output}')
    except OSError as error:
        raise OzfsError(
            f'cannot write output file {output}: {error.strerror}')


def run():
    args = parse_args()
    if not args.check and args.output is None:
        raise OzfsError('one of --check or --output is required')
    if args.check and args.output is not None:
        raise OzfsError('--check and --output cannot be used together')
    if args.output is not None:
        if not args.work_name or not args.work_name.strip():
            raise OzfsError('--work-name is required with --output')
        if '\n' in args.work_name or '\r' in args.work_name:
            raise OzfsError('--work-name must be a single line')

    illumos_path, openzfs_path, lists_dir = load_config(args.config)
    path_lists = load_path_lists(lists_dir)
    platform_mappings = load_platform_mappings(lists_dir)
    illumos_repo = validate_repository(illumos_path, 'illumos')
    openzfs_repo = validate_repository(openzfs_path, 'OpenZFS')
    illumos_commit = resolve_commit(illumos_repo, 'HEAD', 'illumos')
    commits = [
        resolve_commit(openzfs_repo, value, 'OpenZFS')
        for value in args.commits
    ]
    plans = [
        plan_commit(
            illumos_repo, illumos_commit, openzfs_repo, commit, path_lists,
            platform_mappings)
        for commit in commits
    ]

    print(format_manifest(plans), end='')
    if args.output is not None:
        diffs = [format_diff(openzfs_repo, plan) for plan in plans]
        patch = format_combined_patch(
            illumos_repo, args.work_name.strip(), plans, diffs)
        write_patch(args.output, patch)


def main():
    try:
        run()
    except OzfsError as error:
        print(f'ozfs-apply: error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
