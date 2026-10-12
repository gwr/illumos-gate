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
import os
from pathlib import Path
import subprocess
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
    run_git,
    tree_has_path,
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
        '--no-apply', action='store_true',
        help='generate artifacts without invoking git am')
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


def validate_mapped_targets(
        illumos_repo, illumos_commit, openzfs_repo, plans):
    target_exists = {}
    review_additions = {}
    for plan in plans:
        for action in plan.actions:
            if action.kind != 'mapped':
                continue
            source_existed = tree_has_path(
                openzfs_repo, plan.parent, action.source)
            source_exists = tree_has_path(
                openzfs_repo, plan.commit, action.source)
            if action.target not in target_exists:
                target_exists[action.target] = tree_has_path(
                    illumos_repo, illumos_commit, action.target)
            if not source_existed and source_exists:
                if action.group != 'addition':
                    review_additions[
                        (action.source, action.target)] = action
                target_exists[action.target] = True
                continue
            if target_exists[action.target]:
                if source_existed and not source_exists:
                    target_exists[action.target] = False
                continue
            raise OzfsError(
                f'mapped illumos target does not exist: '
                f'{action.source} -> {action.target}')
    return tuple(review_additions.values())


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


def format_review_additions(actions):
    return ''.join(
        f'review-addition\t{action.source}\t{action.target}\n'
        for action in actions
    )


def review_error(actions, patch=None):
    count = len(actions)
    noun = 'path' if count == 1 else 'paths'
    message = f'review required for {count} new illumos {noun}'
    if patch is not None:
        message += f'; patch written to {patch}; git am was not run'
    return OzfsError(message)


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
        return b''

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
        f'Subject: [PATCH] OpenZFS: {work_name}\n'
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


def output_path(path):
    output = path.expanduser()
    if not output.is_absolute():
        output = Path.cwd() / output
    return output.resolve()


def manifest_path(output):
    return Path(f'{output}.manifest.tsv')


def ensure_new_artifacts(output):
    for path in (output, manifest_path(output)):
        if path.exists():
            raise OzfsError(f'output path already exists: {path}')


def write_manifest(path, manifest):
    try:
        with path.open('x', encoding='utf-8') as stream:
            stream.write(manifest)
    except FileExistsError:
        raise OzfsError(f'output path already exists: {path}')
    except OSError as error:
        raise OzfsError(
            f'cannot write output file {path}: {error.strerror}')


def append_manifest(path, text):
    try:
        with path.open('a', encoding='utf-8') as stream:
            stream.write(text)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError as error:
        raise OzfsError(
            f'cannot update manifest {path}: {error.strerror}')


def remove_manifest(path):
    try:
        path.unlink()
    except OSError as error:
        raise OzfsError(
            f'cannot remove manifest {path}: {error.strerror}')


def git_operation_path(repo, name):
    value = git_output(repo, 'rev-parse', '--git-path', name).decode().strip()
    path = Path(value)
    if not path.is_absolute():
        path = repo / path
    return path.resolve()


def ensure_no_git_am(repo):
    if git_operation_path(repo, 'rebase-apply').exists():
        raise OzfsError('git am operation is already in progress')


def ensure_clean(repo):
    status = git_output(
        repo, 'status', '--porcelain=v1', '--untracked-files=no')
    if status:
        raise OzfsError('illumos worktree or index is not clean')


def preimage_oids(openzfs_repo, plans):
    result = set()
    for plan in plans:
        for action in plan.actions:
            if action.kind != 'mapped':
                continue
            resolved = run_git(
                openzfs_repo, 'rev-parse', '--verify', '--end-of-options',
                f'{plan.parent}:{action.source}')
            if resolved.returncode == 0:
                result.add(resolved.stdout.decode().strip())
    return sorted(result)


def copy_preimages(illumos_repo, openzfs_repo, plans):
    for oid in preimage_oids(openzfs_repo, plans):
        blob = git_output(openzfs_repo, 'cat-file', 'blob', oid)
        result = subprocess.run(
            ['git', '-C', str(illumos_repo), 'hash-object', '-w', '--stdin'],
            input=blob, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            check=False)
        if result.returncode != 0:
            message = result.stderr.decode(errors='replace').strip()
            raise OzfsError(f'cannot copy OpenZFS preimage {oid}: {message}')
        copied = result.stdout.decode().strip()
        if copied != oid:
            raise OzfsError(
                f'copied OpenZFS preimage changed object ID: '
                f'{oid} != {copied}')


def apply_patch(illumos_repo, patch):
    result = subprocess.run(
        ['git', '-C', str(illumos_repo), 'am', '--3way', '-'],
        input=patch, check=False)
    if result.returncode == 0:
        return
    if git_operation_path(illumos_repo, 'rebase-apply').exists():
        raise OzfsError(
            'git am stopped; resolve it with Git, then run '
            'git am --continue, --abort, --skip, or --quit')
    raise OzfsError(f'git am failed with exit status {result.returncode}')


def run():
    args = parse_args()
    if args.check and args.output is not None:
        raise OzfsError('--check and --output cannot be used together')
    if args.check and args.no_apply:
        raise OzfsError('--check and --no-apply cannot be used together')
    if args.no_apply and args.output is None:
        raise OzfsError('--output is required with --no-apply')
    if not args.check and not args.no_apply and args.output is not None:
        raise OzfsError('--output may only be used with --no-apply')
    if not args.check:
        if not args.work_name or not args.work_name.strip():
            raise OzfsError('--work-name is required when applying')
        if '\n' in args.work_name or '\r' in args.work_name:
            raise OzfsError('--work-name must be a single line')

    illumos_path, openzfs_path, lists_dir = load_config(args.config)
    path_lists = load_path_lists(lists_dir)
    platform_mappings = load_platform_mappings(lists_dir)
    illumos_repo = validate_repository(illumos_path, 'illumos')
    openzfs_repo = validate_repository(openzfs_path, 'OpenZFS')
    output = None
    operation_manifest = None
    review_patch = None
    if args.no_apply:
        output = output_path(args.output)
        ensure_new_artifacts(output)
    elif not args.check:
        operation_manifest = Path.cwd() / '.ozfs-manifest.tsv'
        review_patch = Path.cwd() / '.ozfs-review.mbox'
        for artifact in (operation_manifest, review_patch):
            if artifact.exists():
                raise OzfsError(
                    f'output path already exists: {artifact}')
        ensure_no_git_am(illumos_repo)
        ensure_clean(illumos_repo)
    illumos_commit = resolve_commit(illumos_repo, 'HEAD', 'illumos')
    commits = [
        resolve_commit(openzfs_repo, value, 'OpenZFS')
        for value in args.commits
    ]
    if operation_manifest is not None:
        initial_manifest = (
            f'work\t{args.work_name.strip()}\n'
            f'illumos\t{illumos_commit}\n' +
            ''.join(f'requested\t{commit}\n' for commit in commits)
        )
        write_manifest(operation_manifest, initial_manifest)

    plans = []
    for commit in commits:
        if operation_manifest is not None:
            append_manifest(operation_manifest, f'planning\t{commit}\n')
        plan = plan_commit(
            illumos_repo, illumos_commit, openzfs_repo, commit, path_lists,
            platform_mappings)
        plans.append(plan)
        if operation_manifest is not None:
            append_manifest(operation_manifest, format_manifest([plan]))

    review_additions = validate_mapped_targets(
        illumos_repo, illumos_commit, openzfs_repo, plans)
    print(format_manifest(plans), end='')
    review_manifest = format_review_additions(review_additions)
    if review_manifest:
        print(review_manifest, end='')
        if operation_manifest is not None:
            append_manifest(operation_manifest, review_manifest)
    if args.check:
        if review_additions:
            raise review_error(review_additions)
        return

    diffs = []
    for plan in plans:
        if operation_manifest is not None:
            append_manifest(
                operation_manifest, f'generating\t{plan.commit}\n')
        diffs.append(format_diff(openzfs_repo, plan))
        if operation_manifest is not None:
            append_manifest(
                operation_manifest, f'generated\t{plan.commit}\n')
    if not any(diffs):
        print('ozfs-apply: no mapped illumos paths; nothing to do')
        if operation_manifest is not None:
            remove_manifest(operation_manifest)
        return
    patch = format_combined_patch(
        illumos_repo, args.work_name.strip(), plans, diffs)
    if output is not None:
        write_manifest(
            manifest_path(output), format_manifest(plans) + review_manifest)
        write_patch(output, patch)
        return

    if review_additions:
        append_manifest(
            operation_manifest, f'review-patch\t{review_patch}\n')
        write_patch(review_patch, patch)
        raise review_error(review_additions, review_patch)

    append_manifest(operation_manifest, 'applying\tgit am --3way -\n')
    copy_preimages(illumos_repo, openzfs_repo, plans)
    apply_patch(illumos_repo, patch)
    remove_manifest(operation_manifest)


def main():
    try:
        run()
    except OzfsError as error:
        print(f'ozfs-apply: error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
