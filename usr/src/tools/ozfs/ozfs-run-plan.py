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
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys


class PlanError(Exception):
    pass


def parse_args():
    parser = argparse.ArgumentParser(
        description='Apply groups from an ozfs-plan JSON file in order')
    parser.add_argument(
        '--plan', type=Path, required=True,
        help='ozfs-plan JSON file')
    parser.add_argument(
        '--cf', '--config', dest='config',
        help='configuration file passed to ozfs-apply')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument(
        '--start', metavar='GROUP_OR_COMMIT',
        help='begin with the selected group')
    selection.add_argument(
        '--after', metavar='GROUP_OR_COMMIT',
        help='begin after the selected group')
    parser.add_argument(
        '--limit', type=positive_integer,
        help='process at most this many groups')
    parser.add_argument(
        '--list', action='store_true',
        help='list selected groups without invoking ozfs-apply')
    parser.add_argument(
        '--ozfs-apply', type=Path,
        default=Path(__file__).with_name('ozfs-apply.py'),
        help='ozfs-apply program (default: adjacent ozfs-apply.py)')
    return parser.parse_args()


def positive_integer(value):
    try:
        number = int(value)
    except ValueError:
        raise argparse.ArgumentTypeError('must be a positive integer')
    if number <= 0:
        raise argparse.ArgumentTypeError('must be a positive integer')
    return number


def read_plan(path):
    try:
        data = path.read_bytes()
    except OSError as error:
        raise PlanError(f'cannot read plan {path}: {error.strerror}')
    try:
        plan = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PlanError(f'cannot parse plan {path}: {error}')
    validate_plan(plan)
    return plan, hashlib.sha256(data).hexdigest()


def require_string(value, description):
    if not isinstance(value, str) or not value:
        raise PlanError(f'{description} must be a nonempty string')
    if '\n' in value or '\r' in value:
        raise PlanError(f'{description} must be a single line')


def validate_plan(plan):
    if not isinstance(plan, dict):
        raise PlanError('plan must be a JSON object')
    version = plan.get('version')
    if version != 1:
        raise PlanError(f'unsupported plan version: {version}')
    groups = plan.get('groups')
    if not isinstance(groups, list):
        raise PlanError('plan groups must be a list')

    group_ids = set()
    commit_ids = set()
    for group_number, group in enumerate(groups, 1):
        if not isinstance(group, dict):
            raise PlanError(f'group {group_number} must be an object')
        group_id = group.get('id')
        require_string(group_id, f'group {group_number} ID')
        if group_id in group_ids:
            raise PlanError(f'duplicate group ID: {group_id}')
        group_ids.add(group_id)
        require_string(group.get('name'), f'group {group_id} name')

        commits = group.get('commits')
        if not isinstance(commits, list) or not commits:
            raise PlanError(f'group {group_id} must contain commits')
        for commit_number, commit in enumerate(commits, 1):
            if not isinstance(commit, dict):
                raise PlanError(
                    f'commit {commit_number} in {group_id} must be an object')
            commit_id = commit.get('commit')
            require_string(
                commit_id, f'commit {commit_number} in {group_id}')
            if (len(commit_id) != 40 or
                    any(character not in '0123456789abcdefABCDEF'
                        for character in commit_id)):
                raise PlanError(
                    f'invalid commit hash in {group_id}: {commit_id}')
            if commit_id in commit_ids:
                raise PlanError(f'duplicate commit hash: {commit_id}')
            commit_ids.add(commit_id)


def find_group(groups, selector):
    matches = [
        index for index, group in enumerate(groups)
        if group['id'] == selector or
        any(commit['commit'] == selector for commit in group['commits'])
    ]
    if not matches:
        raise PlanError(f'selector not found: {selector}')
    if len(matches) > 1:
        raise PlanError(f'selector is ambiguous: {selector}')
    return matches[0]


def select_groups(groups, start, after, limit):
    first = 0
    if start is not None:
        first = find_group(groups, start)
    elif after is not None:
        first = find_group(groups, after) + 1
    selected = groups[first:]
    if limit is not None:
        selected = selected[:limit]
    return selected


def display_group(group):
    count = len(group['commits'])
    noun = 'commit' if count == 1 else 'commits'
    ordinal = group.get('original_ordinal')
    return (
        f"{group['id']}  original {ordinal}  "
        f"{count} {noun}  {group['name']}")


def apply_command(args, group):
    return [
        sys.executable, str(args.ozfs_apply),
        '--cf', args.config,
        '--work-name', f"{group['id']}: {group['name']}",
        *(commit['commit'] for commit in group['commits']),
    ]


def restart_command(args, option, group_id):
    command = [
        sys.executable, sys.argv[0],
        '--plan', str(args.plan),
        '--cf', args.config,
        '--ozfs-apply', str(args.ozfs_apply),
        option, group_id,
    ]
    return shlex.join(command)


def run():
    args = parse_args()
    if not args.list and args.config is None:
        raise PlanError('--cf is required unless --list is used')

    plan, digest = read_plan(args.plan)
    groups = select_groups(
        plan['groups'], args.start, args.after, args.limit)
    print(f'Plan SHA-256: {digest}', flush=True)

    if args.list:
        for group in groups:
            print(display_group(group))
        return 0

    for group in groups:
        print(f"Applying {display_group(group)}", flush=True)
        command = apply_command(args, group)
        try:
            result = subprocess.run(command, check=False)
        except OSError as error:
            raise PlanError(
                f"cannot invoke ozfs-apply for {group['id']}: "
                f'{error.strerror}')
        if result.returncode == 0:
            continue

        if result.returncode < 0:
            status = 128 - result.returncode
            detail = f'terminated by signal {-result.returncode}'
        else:
            status = result.returncode
            detail = f'failed with exit status {result.returncode}'
        print(
            f"ozfs-run-plan: {group['id']} {detail}",
            file=sys.stderr)
        print(
            'Restart this group:\n  ' +
            restart_command(args, '--start', group['id']),
            file=sys.stderr)
        print(
            'If it was completed manually, continue after it:\n  ' +
            restart_command(args, '--after', group['id']),
            file=sys.stderr)
        return status
    return 0


def main():
    try:
        return run()
    except PlanError as error:
        print(f'ozfs-run-plan: error: {error}', file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('ozfs-run-plan: interrupted', file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
