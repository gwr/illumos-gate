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

# Copyright 2026 Gordon W. Ross

import argparse
from dataclasses import dataclass
from email.utils import formataddr
import hashlib
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile

from .Paths import (
    OzfsError,
    PathAction,
    changed_paths,
    classify_path,
    git_output,
    load_config,
    load_path_policy,
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
    author_name: str
    author_email: str
    author_date: str
    actions: tuple


def parse_args():
    parser = argparse.ArgumentParser(
        description='Prepare illumos mail patches from OpenZFS commits')
    parser.add_argument('--config', type=Path, help='local configuration file')
    parser.add_argument('--source', type=Path, help='OpenZFS Git worktree')
    parser.add_argument('--destination', type=Path,
                        help='illumos Git worktree (default: current worktree)')
    parser.add_argument('--import-paths', type=Path,
                        help='override the shipped import path policy file')
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument('--check', action='store_true',
                      help='report path handling without writing anything')
    mode.add_argument('--apply', action='store_true',
                      help='apply one mail patch using its OpenZFS source hashes')
    parser.add_argument('--output', type=Path,
                        help='new mail patch file (default: '
                             '0001-<first-commit-subject>.patch)')
    parser.add_argument('--group', help='optional work label in commit message')
    parser.add_argument('inputs', metavar='commit-or-patch', nargs='+',
                        help='OpenZFS commits to prepare, or one patch with '
                             '--apply')
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
            platform_mappings, preserve_review=True)
        for path in paths
    )
    subject = git_output(
        openzfs_repo, 'show', '-s', '--format=%s', commit
    ).decode().rstrip('\n')
    message = git_output(
        openzfs_repo, 'show', '-s', '--format=%B', commit
    ).decode().replace('\r\n', '\n').replace('\r', '\n').rstrip('\n')
    author_name, author_email, author_date = git_output(
        openzfs_repo, 'show', '-s', '--format=%an%x00%ae%x00%aD', commit
    ).decode().rstrip('\n').split('\0')
    return CommitPlan(
        commit, parent, subject, message, author_name, author_email,
        author_date, actions)


def validate_mapped_targets(
        illumos_repo, illumos_commit, openzfs_repo, plans):
    target_exists = {}
    review_actions = {}
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
                    review = PathAction(
                        'review', action.source, action.target, action.group,
                        'new illumos path')
                    review_actions[
                        (review.source, review.target, review.reason)] = review
                target_exists[action.target] = True
                continue
            if target_exists[action.target]:
                if source_existed and not source_exists:
                    target_exists[action.target] = False
                continue
            review = PathAction(
                'review', action.source, action.target, action.group,
                'mapped illumos target does not exist')
            review_actions[
                (review.source, review.target, review.reason)] = review
            target_exists[action.target] = source_exists
    return tuple(review_actions.values())


def format_actions(actions):
    lines = []
    for action in actions:
        fields = [action.kind, action.source]
        if action.target:
            fields.append(action.target)
        if action.reason:
            fields.append(action.reason)
        lines.append('\t'.join(fields))
    if not lines:
        return ''
    return '\n'.join(lines) + '\n'


def format_manifest(plans):
    text = ''
    for plan in plans:
        text += f'commit\t{plan.commit}\t{plan.subject}\n'
        text += format_actions(plan.actions)
    return text


def format_review_actions(actions):
    return format_actions(actions)


def review_error(actions, patch=None):
    count = len(actions)
    noun = 'path' if count == 1 else 'paths'
    message = f'review required for {count} {noun}'
    if patch is not None:
        message += f'; patch written to {patch}; git am was not run'
    return OzfsError(message)


def quoted_path(path):
    data = path.encode()
    if all(32 <= byte < 127 and byte not in (34, 92) for byte in data):
        return data
    escapes = {7: b'\\a', 8: b'\\b', 9: b'\\t', 10: b'\\n',
               11: b'\\v', 12: b'\\f', 13: b'\\r',
               34: b'\\"', 92: b'\\\\'}
    escaped = b''.join(
        escapes.get(byte, bytes([byte]) if 32 <= byte < 127
                    else ('\\%03o' % byte).encode())
        for byte in data)
    return b'"' + escaped + b'"'


def rewrite_patch_paths(patch, mappings):
    replacements = {}
    for source, target in mappings:
        source_a, source_b = (quoted_path(f'{side}/{source}')
                              for side in ('a', 'b'))
        target_a, target_b = (quoted_path(f'{side}/{target}')
                              for side in ('a', 'b'))
        replacements[b'diff --git ' + source_a + b' ' + source_b] = (
            b'diff --git ' + target_a + b' ' + target_b)
        replacements[b'--- ' + source_a] = b'--- ' + target_a
        replacements[b'+++ ' + source_b] = b'+++ ' + target_b

    # Rewrite only metadata, never a similarly spelled removed line or binary
    # payload.  One lookup per line also avoids cascading path substitutions.
    metadata = False
    result = []
    for line in patch.splitlines(keepends=True):
        if line.startswith(b'diff --git '):
            metadata = True
        elif line.startswith((b'@@ ', b'GIT binary patch')):
            metadata = False
        if metadata:
            text = line[:-1] if line.endswith(b'\n') else line
            # Git appends a tab to an unquoted filename containing spaces.
            text = text[:-1] if text.endswith(b'\t') else text
            line = replacements.get(text, line.rstrip(b'\n')) + b'\n'
        result.append(line)
    return b''.join(result)


def format_diff(openzfs_repo, plan):
    mappings = [
        (action.source, action.target)
        for action in plan.actions
        if action.kind in ('mapped', 'review')
    ]
    if not mappings:
        return b''

    patch = git_output(
        openzfs_repo, '--literal-pathspecs', '-c', 'core.quotePath=true',
        'diff', '--full-index',
        '--binary', '--no-renames', '--no-ext-diff', '--no-textconv',
        '--no-color',
        plan.parent, plan.commit, '--',
        *(source for source, _target in mappings))
    return rewrite_patch_paths(patch, mappings)


def quote_mbox_from_lines(text):
    return '\n'.join(
        f'>{line}' if line.startswith('From ') else line
        for line in text.split('\n')
    )


def format_patch_index(illumos_repo, diffs, review_actions):
    summary = git_output(
        illumos_repo, 'apply', '--stat', '--summary',
        input_data=b''.join(diffs))
    lines = []
    seen = set()
    for action in review_actions:
        key = (action.source, action.target, action.reason)
        if key in seen:
            continue
        seen.add(key)
        source = quoted_path(action.source).decode()
        target = quoted_path(action.target).decode()
        path = f'{source} -> {target}' if target and target != source else source
        lines.extend((f'  {path}', f'    {action.reason}'))
    if lines:
        summary += ('\nRequires review:\n' + '\n'.join(lines) + '\n').encode()
    return summary + b'\n'


def format_combined_patch(illumos_repo, group, plans, diffs, review_actions):
    primary = plans[0]
    body = [f'Import {group}', ''] if group else []
    for plan in plans:
        body.extend((
            f'OpenZFS {plan.commit}',
            '',
            quote_mbox_from_lines(plan.message),
            '',
        ))

    headers = (
        f'From {primary.commit} Mon Sep 17 00:00:00 2001\n'
        f'From: {formataddr((primary.author_name, primary.author_email))}\n'
        f'Date: {primary.author_date}\n'
        f'Subject: [PATCH] {primary.subject}\n'
        '\n'
    )
    message = headers + '\n'.join(body) + '---\n'
    index = format_patch_index(illumos_repo, diffs, review_actions)
    return message.encode() + index + b''.join(diffs)


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


def copy_blob_oids(illumos_repo, openzfs_repo, oids):
    for oid in oids:
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


def read_patch_sources(repo, patch):
    # Let Git decode mail and distinguish the commit message from the diff.
    # Scratch files are small mail artifacts under this worktree's Git area.
    with tempfile.TemporaryDirectory(
            prefix='ozfs-import-mail-', dir=git_operation_path(repo, '.')) as tmp:
        directory = Path(tmp)
        split = git_output(repo, 'mailsplit', '-b', f'-o{directory}',
                           input_data=patch)
        if split.strip() != b'1':
            raise OzfsError('--apply requires exactly one mail patch')
        message_path = directory / 'message'
        diff_path = directory / 'diff'
        git_output(repo, 'mailinfo', str(message_path), str(diff_path),
                   input_data=(directory / '0001').read_bytes())
        message = message_path.read_bytes()
        diff = diff_path.read_bytes()
    commits = list(dict.fromkeys(
        match.decode().lower() for match in re.findall(
            rb'^OpenZFS ([0-9a-fA-F]{40}|[0-9a-fA-F]{64})\r?$',
            message, re.MULTILINE)))
    if not commits:
        raise OzfsError('patch commit message has no full OpenZFS hashes')
    if not diff:
        raise OzfsError('mail patch contains no diff')
    return commits, diff


def required_preimages(openzfs_repo, illumos_repo, commits, diff):
    candidates = set()
    for commit in commits:
        parent = commit_parent(openzfs_repo, commit)
        for path in changed_paths(openzfs_repo, parent, commit):
            result = run_git(openzfs_repo, 'rev-parse', '--verify',
                             '--end-of-options', f'{parent}:{path}')
            if result.returncode == 0:
                candidates.add(result.stdout.decode().strip())

    needed = set()
    metadata = False
    for line in diff.splitlines():
        if line.startswith(b'diff --git '):
            metadata = True
        elif line.startswith((b'@@ ', b'GIT binary patch')):
            metadata = False
        if not metadata:
            continue
        match = re.fullmatch(
            rb'index ([0-9a-f]{7,64})\.\.([0-9a-f]{7,64})(?: [0-7]{6})?',
            line)
        if not match or not match[1].strip(b'0'):
            continue
        prefix = match[1].decode()
        # A reviewed patch may use an existing native preimage instead.
        if run_git(illumos_repo, 'cat-file', '-e',
                   f'{prefix}^{{blob}}').returncode == 0:
            continue
        matches = [oid for oid in candidates if oid.startswith(prefix)]
        if len(matches) != 1:
            raise OzfsError(
                f'patch preimage {prefix} is not uniquely provided by '
                'the OpenZFS commits in its message')
        oid = matches[0]
        if git_output(openzfs_repo, 'cat-file', '-t', oid).strip() != b'blob':
            raise OzfsError(f'patch preimage {oid} is not a blob')
        needed.add(oid)
    return sorted(needed)


def apply_mail_patch(illumos_repo, openzfs_repo, path):
    operation_manifest = git_operation_path(
        illumos_repo, 'ozfs-import.manifest.tsv')
    if operation_manifest.exists():
        raise OzfsError(f'output path already exists: {operation_manifest}')
    ensure_no_git_am(illumos_repo)
    ensure_clean(illumos_repo)
    patch = path.expanduser().resolve().read_bytes()
    hashes, diff = read_patch_sources(illumos_repo, patch)
    commits = [resolve_commit(openzfs_repo, value, 'OpenZFS')
               for value in hashes]
    preimages = required_preimages(openzfs_repo, illumos_repo, commits, diff)
    head = resolve_commit(illumos_repo, 'HEAD', 'illumos')
    write_manifest(operation_manifest,
                   f'illumos\t{head}\npatch\t{path.resolve()}\n'
                   f'sha256\t{hashlib.sha256(patch).hexdigest()}\n' +
                   ''.join(f'requested\t{commit}\n' for commit in commits))
    append_manifest(operation_manifest, 'copying\tpreimage blobs\n')
    copy_blob_oids(illumos_repo, openzfs_repo, preimages)
    append_manifest(operation_manifest, 'applying\tgit am --3way -\n')
    apply_patch(illumos_repo, patch)
    remove_manifest(operation_manifest)


def run(defaults):
    args = parse_args()
    if (args.check or args.apply) and args.output is not None:
        raise OzfsError('--output cannot be used with --check or --apply')
    if args.group is not None and (
            not args.group.strip() or
            any(ord(char) < 32 or ord(char) == 127 for char in args.group)):
        raise OzfsError('--group must be a nonempty single-line label')

    illumos_path, openzfs_path, policy_path = load_config(
        args.config, defaults, args.source, args.destination, args.import_paths)
    illumos_repo = validate_repository(illumos_path, 'illumos')
    openzfs_repo = validate_repository(openzfs_path, 'OpenZFS')
    if illumos_repo == openzfs_repo:
        raise OzfsError('source and destination must be different repositories')
    if args.apply:
        if len(args.inputs) != 1:
            raise OzfsError('--apply requires one patch filename')
        if args.group is not None or args.import_paths is not None:
            raise OzfsError('--group and --import-paths are preparation options')
        apply_mail_patch(illumos_repo, openzfs_repo, Path(args.inputs[0]))
        return
    path_lists, platform_mappings = load_path_policy(policy_path)
    output = None
    preparing = not args.check and not args.apply
    if preparing:
        if args.output is not None:
            output = output_path(args.output)
            ensure_new_artifacts(output)
    illumos_commit = resolve_commit(illumos_repo, 'HEAD', 'illumos')
    commits = [
        resolve_commit(openzfs_repo, value, 'OpenZFS')
        for value in args.inputs
    ]
    if preparing and output is None:
        subject = git_output(
            openzfs_repo, 'show', '-s', '--format=%f', commits[0]
        ).decode().strip()
        # Keep the whole filename within Git's usual 64-character limit.
        subject = subject[:53].rstrip('.-') or 'patch'
        output = output_path(Path(f'0001-{subject}.patch'))
        ensure_new_artifacts(output)
    plans = [plan_commit(
        illumos_repo, illumos_commit, openzfs_repo, commit, path_lists,
        platform_mappings) for commit in commits]

    validation_reviews = validate_mapped_targets(
        illumos_repo, illumos_commit, openzfs_repo, plans)
    review_actions = tuple(
        action
        for plan in plans
        for action in plan.actions
        if action.kind == 'review'
    ) + validation_reviews
    print(format_manifest(plans), end='')
    review_manifest = format_review_actions(validation_reviews)
    if review_manifest:
        print(review_manifest, end='')
    if args.check:
        if review_actions:
            raise review_error(review_actions)
        return

    diffs = [format_diff(openzfs_repo, plan) for plan in plans]
    if not any(diffs):
        print('ozfs-import: no mapped illumos paths; nothing to do')
        return
    patch = format_combined_patch(
        illumos_repo, args.group, plans, diffs, review_actions)
    if output is not None:
        write_manifest(
            manifest_path(output),
            f'illumos\t{illumos_commit}\n' +
            format_manifest(plans) + review_manifest)
        write_patch(output, patch)
        if review_actions:
            raise review_error(review_actions, output)
        print(f'ozfs-import: prepared {output}')
        return


def main(defaults):
    try:
        run(defaults)
    except (OzfsError, OSError, UnicodeError) as error:
        print(f'ozfs-import: error: {error}', file=sys.stderr)
        return 1
    return 0
