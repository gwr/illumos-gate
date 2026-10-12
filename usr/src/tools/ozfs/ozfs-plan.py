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
from bisect import bisect_right
from dataclasses import dataclass, field
from datetime import datetime
from email.utils import parsedate_to_datetime
import json
import os
from pathlib import Path
import re
import sys
import tempfile


PLAN_VERSION = 1
HEURISTIC_DAYS = 90
EXPLICIT_DAYS = 180

COMMIT_HEADER_RE = re.compile(
    r'(?m)^commit ([0-9a-f]{40})(?: .*)?\n')
NUMSTAT_RE = re.compile(r'^(?:\d+|-)\t(?:\d+|-)\t(.+)$')
CLOSE_RE = re.compile(r'(?im)^\s*(?:Closes|Fixes)\s+#(\d+)\s*$')
EXPLICIT_PR_RE = re.compile(
    r'(?i)(follow(?:-|\s)?(?:up|on)(?:\s+(?:fix|patch|commit))?'
    r'\s+(?:to|for)\s+(?:PR\s*)?#(\d+))')
EXPLICIT_COMMIT_RES = (
    re.compile(
        r'(?i)(follow(?:-|\s)?(?:up|on)(?:\s+(?:fix|patch|commit))?'
        r'\s+(?:to|for)\s+(?:commit\s+)?([0-9a-f]{7,40}))'),
    re.compile(
        r'(?i)((?:introduced|caused)\s+by\s+(?:commit\s+)?'
        r'([0-9a-f]{7,40}))'),
    re.compile(
        r'(?i)((?:due\s+to|regression\s+from)\s+(?:commit\s+)?'
        r'([0-9a-f]{7,40}))'),
    re.compile(
        r'(?i)((?:this\s+)?reverts?\s+(?:commit\s+)?'
        r'([0-9a-f]{7,40}))'),
    re.compile(
        r'(?i)(commit\s+([0-9a-f]{7,40})\s+introduced\s+'
        r'(?:a\s+)?regression)'),
)
CLUE_RES = (
    ('follow-up', re.compile(r'(?i)\bfollow[- ]?up\b')),
    ('introduced-by', re.compile(
        r'(?i)\b(?:introduced\s+by|caused\s+by|regression)\b')),
    ('revert', re.compile(r'(?i)\brevert(?:s|ed|ing)?\b')),
    ('reapply-backout', re.compile(
        r'(?i)\b(?:re[- ]?appl(?:y|ied)|back[- ]?out|'
        r'backed\s+out)\b')),
    ('supersede', re.compile(r'(?i)\bsupersed(?:e|es|ed|ing)\b')),
    ('named-later-fix', re.compile(
        r'(?i)\bfixes\s+this\s+problem\b')),
)
PARTIAL_REVERT_RE = re.compile(
    r'(?i)\b(?:partial(?:ly)?\s+revert(?:s|ed|ing)?|'
    r'revert(?:s|ed|ing)?\s+(?:part|parts)\s+of)\b')


class PlanError(Exception):
    pass


@dataclass(frozen=True)
class Commit:
    ordinal: int
    commit: str
    author: str
    date: datetime
    subject: str
    message: str
    paths: tuple
    closes: tuple
    clues: tuple


@dataclass
class PlannedCommit:
    commit: Commit
    movement: dict = None


@dataclass
class Group:
    commits: list = field(default_factory=list)
    removed: bool = False

    @property
    def first_ordinal(self):
        return min(item.commit.ordinal for item in self.commits)

    @property
    def last_ordinal(self):
        return max(item.commit.ordinal for item in self.commits)


def parse_args():
    parser = argparse.ArgumentParser(
        description='Build and edit a logical OpenZFS commit plan')
    parser.add_argument(
        '--logn', type=Path, required=True,
        help='newest-first getlog output containing git numstat data')
    parser.add_argument(
        '--plan', type=Path, required=True,
        help='JSON plan to create or update')
    parser.add_argument(
        '--markdown', type=Path,
        help='optional readable rendering of the resulting plan')
    return parser.parse_args()


def parse_record(ordinal, commit_hash, text):
    lines = text.splitlines()
    author = next(
        (line[len('Author: '):] for line in lines
         if line.startswith('Author: ')),
        None)
    date_text = next(
        (line[len('Date:   '):] for line in lines
         if line.startswith('Date:   ')),
        None)
    if author is None:
        raise PlanError(f'commit {commit_hash} has no Author header')
    if date_text is None:
        raise PlanError(f'commit {commit_hash} has no Date header')
    try:
        date = parsedate_to_datetime(date_text)
    except (TypeError, ValueError) as error:
        raise PlanError(
            f'commit {commit_hash} has an invalid date: {date_text}'
        ) from error

    message_lines = []
    paths = []
    for line in lines:
        if line.startswith('    '):
            message_lines.append(line[4:])
            continue
        match = NUMSTAT_RE.match(line)
        if match:
            paths.append(match.group(1))
    message = '\n'.join(message_lines).strip()
    subject = next(
        (line for line in message_lines if line),
        '')
    if not subject:
        raise PlanError(f'commit {commit_hash} has no subject')
    closes = tuple(dict.fromkeys(CLOSE_RE.findall(message)))
    clues = tuple(
        name for name, pattern in CLUE_RES
        if pattern.search(message))
    return Commit(
        ordinal, commit_hash, author, date, subject, message,
        tuple(dict.fromkeys(paths)), closes, clues)


def parse_logn(path):
    try:
        text = path.read_text(encoding='utf-8')
    except OSError as error:
        raise PlanError(f'cannot read {path}: {error}') from error
    parts = COMMIT_HEADER_RE.split(text)
    if len(parts) < 3:
        raise PlanError(f'{path} contains no full commit records')
    if parts[0].strip():
        raise PlanError(f'{path} has content before its first commit')

    newest_first = []
    for index in range(1, len(parts), 2):
        newest_first.append((parts[index], parts[index + 1]))
    records = []
    for ordinal, (commit_hash, record) in enumerate(
            reversed(newest_first), 1):
        records.append(parse_record(ordinal, commit_hash, record))
    return records


def load_decisions(path):
    if not path.exists():
        return []
    try:
        plan = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError) as error:
        raise PlanError(f'cannot read existing plan {path}: {error}') from error
    if plan.get('version') != PLAN_VERSION:
        raise PlanError(
            f'{path} has unsupported plan version {plan.get("version")}')
    decisions = plan.get('decisions', [])
    if not isinstance(decisions, list):
        raise PlanError(f'{path} decisions must be a list')
    return decisions


def initial_groups(commits):
    groups = []
    for commit in commits:
        if (
                groups and commit.closes and
                groups[-1].commits[-1].commit.closes == commit.closes):
            groups[-1].commits.append(PlannedCommit(commit))
        else:
            groups.append(Group([PlannedCommit(commit)]))
    return groups


def group_map(groups):
    return {
        item.commit.commit: group
        for group in groups if not group.removed
        for item in group.commits
    }


def resolve_hash(reference, hashes):
    matches = [commit_hash for commit_hash in hashes
               if commit_hash.startswith(reference.lower())]
    if len(matches) == 1:
        return matches[0]
    return None


def elapsed_days(first, second):
    return round(abs((second.date - first.date).total_seconds()) / 86400, 2)


def shared_intervening_commits(
        commits, target_group, source_commit, first, last):
    target_paths = {
        path
        for item in target_group.commits
        for path in item.commit.paths
    }
    shared = target_paths & set(source_commit.paths)
    if not shared:
        return []
    return [
        commit.commit
        for commit in commits
        if first < commit.ordinal < last and shared.intersection(commit.paths)
    ]


def movement_record(
        commits, target_group, source, trigger, evidence):
    target = max(
        (item.commit for item in target_group.commits
         if item.commit.ordinal < source.ordinal),
        key=lambda commit: commit.ordinal,
        default=max(
            (item.commit for item in target_group.commits),
            key=lambda commit: commit.ordinal))
    return {
        'original_ordinal': source.ordinal,
        'distance_commits': abs(source.ordinal - target.ordinal),
        'distance_days': elapsed_days(target, source),
        'trigger': trigger,
        'evidence': evidence,
        'target_commit': target.commit,
        'intervening_same_file_commits':
            shared_intervening_commits(
                commits, target_group, source,
                target.ordinal, source.ordinal),
    }


def attach_group(
        commits, target_group, source_group, trigger, evidence):
    if target_group is source_group:
        return
    for item in source_group.commits:
        item.movement = movement_record(
            commits, target_group, item.commit, trigger, evidence)
    target_group.commits.extend(source_group.commits)
    target_group.commits.sort(key=lambda item: item.commit.ordinal)
    source_group.removed = True


def validate_decision(decision, hashes):
    if not isinstance(decision, dict):
        raise PlanError('each decision must be an object')
    action = decision.get('action')
    if action not in ('attach', 'separate'):
        raise PlanError(f'unsupported decision action: {action}')
    commit_hash = decision.get('commit')
    target = decision.get('target')
    if commit_hash not in hashes:
        raise PlanError(f'decision names unknown commit: {commit_hash}')
    if target not in hashes:
        raise PlanError(f'decision names unknown target: {target}')
    if commit_hash == target:
        raise PlanError(f'decision cannot target itself: {commit_hash}')


def apply_manual_decisions(commits, groups, decisions):
    hashes = {commit.commit for commit in commits}
    for decision in decisions:
        validate_decision(decision, hashes)
    for decision in decisions:
        if decision['action'] != 'attach':
            continue
        by_commit = group_map(groups)
        source = by_commit[decision['commit']]
        target = by_commit[decision['target']]
        if source is target:
            continue
        if len(source.commits) != 1:
            raise PlanError(
                'manual attach cannot move a multi-commit group: '
                f'{decision["commit"]}')
        attach_group(
            commits, target, source, 'manual-attach',
            decision.get('reason', 'manual decision'))


def separate_pairs(decisions):
    return {
        (decision['commit'], decision['target'])
        for decision in decisions
        if decision['action'] == 'separate'
    }


def group_for_pr(groups, pr, before_ordinal):
    candidates = []
    for group in groups:
        if group.removed:
            continue
        matching = [
            item.commit
            for item in group.commits
            if pr in item.commit.closes and
            item.commit.ordinal < before_ordinal
        ]
        if matching:
            candidates.append((
                max(commit.ordinal for commit in matching), group))
    if not candidates:
        return None
    return max(candidates, key=lambda item: item[0])[1]


def explicit_relation(commit, groups, hashes):
    match = EXPLICIT_PR_RE.search(commit.message)
    if match:
        target = group_for_pr(groups, match.group(2), commit.ordinal)
        if target is not None:
            return 'explicit-pr', match.group(1), target

    for pattern in EXPLICIT_COMMIT_RES:
        match = pattern.search(commit.message)
        if match:
            target_hash = resolve_hash(match.group(2), hashes)
            if target_hash is not None:
                target = group_map(groups)[target_hash]
                if target.first_ordinal < commit.ordinal:
                    return 'explicit-commit', match.group(1), target
    return None


def apply_explicit_relations(commits, groups, decisions):
    candidates = []
    hashes = [commit.commit for commit in commits]
    blocked = separate_pairs(decisions)
    manually_attached = {
        decision['commit']
        for decision in decisions
        if decision['action'] == 'attach'
    }
    for commit in commits:
        if commit.commit in manually_attached:
            continue
        relation = explicit_relation(commit, groups, hashes)
        if relation is None:
            continue
        trigger, evidence, target = relation
        by_commit = group_map(groups)
        source = by_commit[commit.commit]
        if source is target:
            continue
        target_hashes = {
            item.commit.commit for item in target.commits
        }
        blocked_target = next(
            (target_hash for source_hash, target_hash in blocked
             if source_hash == commit.commit and
             target_hash in target_hashes),
            None)
        if blocked_target is not None:
            continue
        target_commit = max(
            (item.commit for item in target.commits
             if item.commit.ordinal < commit.ordinal),
            key=lambda item: item.ordinal)
        days = elapsed_days(target_commit, commit)
        candidate_trigger = None
        if PARTIAL_REVERT_RE.search(commit.message):
            candidate_trigger = 'partial-revert'
        elif len(source.commits) != 1:
            candidate_trigger = f'grouped-{trigger}'
        elif days > EXPLICIT_DAYS:
            candidate_trigger = f'expired-{trigger}'
        if candidate_trigger is not None:
            candidates.append({
                'commit': commit.commit,
                'target_commit': target_commit.commit,
                'target_group': None,
                'trigger': candidate_trigger,
                'evidence': evidence,
                'distance_commits':
                    commit.ordinal - target_commit.ordinal,
                'distance_days': days,
                'paths': sorted(
                    set(commit.paths) & {
                        path
                        for item in target.commits
                        for path in item.commit.paths
                    }),
            })
            continue
        attach_group(commits, target, source, trigger, evidence)
    return candidates


def same_file_candidates(commits, groups):
    path_ordinals = {}
    by_ordinal = {commit.ordinal: commit for commit in commits}
    for commit in commits:
        for path in commit.paths:
            path_ordinals.setdefault(path, []).append(commit.ordinal)

    candidates = []
    by_commit = group_map(groups)
    for group in groups:
        if group.removed:
            continue
        last = group.last_ordinal
        next_paths = {}
        for path in {
                path
                for item in group.commits
                for path in item.commit.paths}:
            ordinals = path_ordinals[path]
            index = bisect_right(ordinals, last)
            if index < len(ordinals):
                next_paths.setdefault(ordinals[index], []).append(path)
        for ordinal, paths in sorted(next_paths.items()):
            candidate = by_ordinal[ordinal]
            if by_commit[candidate.commit] is group:
                continue
            target_commit = max(
                (item.commit for item in group.commits),
                key=lambda item: item.ordinal)
            days = elapsed_days(target_commit, candidate)
            if days > HEURISTIC_DAYS:
                continue
            candidates.append({
                'commit': candidate.commit,
                'target_commit': target_commit.commit,
                'target_group': None,
                'trigger': 'next-same-file',
                'evidence': (
                    f'next later commit touching {len(paths)} '
                    f'group file{"s" if len(paths) != 1 else ""}'),
                'distance_commits': candidate.ordinal - last,
                'distance_days': days,
                'paths': sorted(paths),
                'clues': list(candidate.clues),
            })
    return candidates


def clue_evidence(commit):
    evidence = []
    for line in commit.message.splitlines():
        matching = [
            name for name, pattern in CLUE_RES
            if pattern.search(line)
        ]
        if matching:
            evidence.append({
                'clues': matching,
                'text': line,
            })
    return evidence


def clue_candidate(commit):
    references = list(dict.fromkeys(
        re.findall(r'\b[0-9a-f]{7,40}\b|#\d+', commit.message)))
    body = commit.message.splitlines()[1:]
    while body and not body[0]:
        body.pop(0)
    return {
        'commit': commit.commit,
        'original_ordinal': commit.ordinal,
        'date': commit.date.isoformat(),
        'subject': commit.subject,
        'clues': list(commit.clues),
        'references': references,
        'message_opening': body[:10],
        'evidence': clue_evidence(commit),
    }


def commit_json(item):
    commit = item.commit
    return {
        'commit': commit.commit,
        'original_ordinal': commit.ordinal,
        'date': commit.date.isoformat(),
        'author': commit.author,
        'subject': commit.subject,
        'closes': list(commit.closes),
        'clues': list(commit.clues),
        'paths': list(commit.paths),
        'movement': item.movement,
    }


def build_plan(logn, commits, decisions):
    groups = initial_groups(commits)
    apply_manual_decisions(commits, groups, decisions)
    candidates = apply_explicit_relations(commits, groups, decisions)
    candidates.extend(same_file_candidates(commits, groups))

    active = sorted(
        (group for group in groups if not group.removed),
        key=lambda group: group.first_ordinal)
    group_ids = {
        id(group): f'G{number:04d}'
        for number, group in enumerate(active, 1)
    }
    by_commit = group_map(active)
    for candidate in candidates:
        target = by_commit.get(candidate['target_commit'])
        candidate['target_group'] = (
            group_ids[id(target)] if target is not None else None)
    candidates.sort(key=lambda item: (
        item['target_group'] or '', item['commit'], item['trigger']))

    return {
        'version': PLAN_VERSION,
        'source': str(logn),
        'commit_count': len(commits),
        'clue_commit_count': sum(bool(commit.clues) for commit in commits),
        'policy': {
            'heuristic_days': HEURISTIC_DAYS,
            'explicit_days': EXPLICIT_DAYS,
            'same_file_action': 'candidate',
        },
        'decisions': decisions,
        'groups': [
            {
                'id': group_ids[id(group)],
                'name': group.commits[0].commit.subject,
                'commits': [commit_json(item) for item in group.commits],
            }
            for group in active
        ],
        'clue_candidates': [
            clue_candidate(commit)
            for commit in commits if commit.clues
        ],
        'candidates': candidates,
    }


def write_text_atomic(path, text):
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(
                mode='w', encoding='utf-8', dir=path.parent,
                prefix=f'.{path.name}.', delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(text)
        os.replace(temporary, path)
    except OSError as error:
        try:
            temporary.unlink()
        except (NameError, OSError):
            pass
        raise PlanError(f'cannot write {path}: {error}') from error


def format_markdown(plan):
    moved = sum(
        item['movement'] is not None
        for group in plan['groups']
        for item in group['commits'])
    lines = [
        '# OpenZFS commit plan',
        '',
        f'- Source commits: {plan["commit_count"]}',
        f'- Logical groups: {len(plan["groups"])}',
        f'- Moved commits: {moved}',
        f'- Wording-clue commits: {plan["clue_commit_count"]}',
        f'- Review candidates: {len(plan["candidates"])}',
        f'- Heuristic window: {plan["policy"]["heuristic_days"]} days',
        f'- Explicit-reference window: '
        f'{plan["policy"]["explicit_days"]} days',
        '',
        'The JSON plan is authoritative. Add `attach` or `separate` '
        'objects to its `decisions` list and rerun the tool to preserve '
        'manual judgments.',
        '',
        '## Groups',
        '',
    ]
    candidates = {}
    for candidate in plan['candidates']:
        candidates.setdefault(candidate['target_group'], []).append(candidate)
    for group in plan['groups']:
        lines.extend((
            f'### {group["id"]} {group["name"]}',
            '',
        ))
        for item in group['commits']:
            line = (
                f'- `{item["commit"]}` '
                f'(original {item["original_ordinal"]:04d}) '
                f'{item["subject"]}')
            if item['movement'] is not None:
                movement = item['movement']
                line += (
                    f' — **Moved from '
                    f'{movement["original_ordinal"]:04d}** by '
                    f'`{movement["trigger"]}`: '
                    f'{movement["evidence"]}')
            elif item['clues']:
                line += (
                    ' — wording clues='
                    f'{",".join(item["clues"])}')
            lines.append(line)
        for candidate in candidates.get(group['id'], []):
            trigger = candidate['trigger']
            risky = (
                trigger == 'partial-revert' or
                trigger.startswith('grouped-explicit-') or
                trigger.startswith('expired-explicit-') or
                trigger.startswith('conflicting-'))
            marker = (
                f'**XXX-illumos: not moved ({trigger})**'
                if risky else f'Candidate `{trigger}`')
            lines.append(
                f'- {marker} '
                f'`{candidate["commit"]}` after '
                f'{candidate["distance_days"]:.2f} days: '
                f'{candidate["evidence"]}')
        lines.append('')
    return '\n'.join(lines)


def main():
    args = parse_args()
    try:
        commits = parse_logn(args.logn)
        decisions = load_decisions(args.plan)
        plan = build_plan(args.logn, commits, decisions)
        write_text_atomic(
            args.plan, json.dumps(plan, indent=2) + '\n')
        if args.markdown is not None:
            write_text_atomic(args.markdown, format_markdown(plan))
    except PlanError as error:
        print(f'ozfs-plan: {error}', file=sys.stderr)
        return 2
    print(
        f'{len(commits)} commits -> {len(plan["groups"])} groups; '
        f'{len(plan["candidates"])} candidates')
    return 0


if __name__ == '__main__':
    sys.exit(main())
