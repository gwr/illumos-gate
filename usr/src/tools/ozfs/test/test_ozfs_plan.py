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

import json
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from email.utils import format_datetime
from pathlib import Path


SCRIPT = Path(__file__).parent.parent / 'ozfs-plan.py'


class OzfsPlanTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.logn = self.root / 'history.logn'
        self.plan = self.root / 'plan.json'
        self.markdown = self.root / 'plan.md'

    def tearDown(self):
        self.tempdir.cleanup()

    def _commit(self, number, date, subject, body='', paths=()):
        commit = f'{number:040x}'
        message = subject
        if body:
            message += f'\n\n{body}'
        indented = '\n'.join(f'    {line}' for line in message.splitlines())
        numstat = '\n'.join(f'1\t1\t{path}' for path in paths)
        return (
            f'commit {commit}\n'
            f'Author: Test Author <test@example.invalid>\n'
            f'Date:   {format_datetime(date)}\n\n'
            f'{indented}\n\n'
            f'{numstat}\n'
        )

    def _write_log(self, commits):
        self.logn.write_text(
            '\n'.join(reversed(commits)), encoding='utf-8')

    def _run(self):
        return subprocess.run(
            [
                sys.executable, str(SCRIPT),
                '--logn', str(self.logn),
                '--plan', str(self.plan),
                '--markdown', str(self.markdown),
            ],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def _load_plan(self):
        return json.loads(self.plan.read_text(encoding='utf-8'))

    def test_groups_same_pr_and_pulls_forward_explicit_pr_followup(self):
        dates = [
            datetime(2020, 1, day, tzinfo=timezone.utc)
            for day in range(1, 6)
        ]
        commits = [
            self._commit(
                1, dates[0], 'first part', 'Closes #100', ('a.c',)),
            self._commit(
                2, dates[1], 'second part', 'Closes #100', ('b.c',)),
            self._commit(
                3, dates[2], 'unrelated', 'Closes #101', ('other.c',)),
            self._commit(
                4, dates[3], 'complete first work',
                'This is a follow-on to PR #100.\n\nCloses #102',
                ('a.c',)),
            self._commit(
                5, dates[4], 'last work', 'Closes #103', ('last.c',)),
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(plan['commit_count'], 5)
        self.assertEqual(len(plan['groups']), 4)
        first = plan['groups'][0]
        self.assertEqual(
            [item['commit'] for item in first['commits']],
            [f'{number:040x}' for number in (1, 2)])
        moved = plan['groups'][1]
        self.assertEqual(
            [item['commit'] for item in moved['commits']],
            [f'{4:040x}'])
        self.assertEqual(moved['moved_after'], 'G0001')
        self.assertEqual(moved['todo'], 'squash with previous')
        movement = moved['commits'][0]['movement']
        self.assertEqual(movement['trigger'], 'explicit-pr')
        self.assertEqual(movement['original_ordinal'], 4)
        self.assertEqual(movement['distance_days'], 2.0)
        self.assertEqual(movement['evidence'], 'follow-on to PR #100')
        markdown = self.markdown.read_text()
        self.assertIn('Moved from 0004', markdown)
        self.assertIn('### NO-COMMIT original 0004', markdown)
        self.assertIn('Moved to G0002 after G0001', markdown)
        self.assertIn('Todo: squash with previous', markdown)
        self.assertNotIn('XXX-illumos', markdown)

    def test_accepts_decorated_commit_header(self):
        commit = self._commit(
            1, datetime(2020, 1, 1, tzinfo=timezone.utc),
            'decorated', paths=('a.c',))
        commit = commit.replace(
            f'commit {1:040x}\n',
            f'commit {1:040x} (HEAD -> test-branch)\n')
        self._write_log([commit])

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self._load_plan()['commit_count'], 1)

    def test_pulls_forward_explicit_hash_within_six_months(self):
        original = f'{1:040x}'
        commits = [
            self._commit(
                1, datetime(2020, 1, 1, tzinfo=timezone.utc),
                'introduce behavior', paths=('a.c',)),
            self._commit(
                2, datetime(2020, 2, 1, tzinfo=timezone.utc),
                'other work', paths=('other.c',)),
            self._commit(
                3, datetime(2020, 3, 1, tzinfo=timezone.utc),
                'fix behavior',
                f'Fix regression introduced by commit {original}.',
                ('a.c',)),
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(len(plan['groups']), 3)
        self.assertEqual(
            [group['commits'][0]['commit'] for group in plan['groups']],
            [f'{number:040x}' for number in (1, 3, 2)])
        self.assertEqual(
            plan['groups'][1]['commits'][0]['movement']['trigger'],
            'explicit-commit')

    def test_marks_explicit_relation_without_moving_pr_series(self):
        commits = [
            self._commit(
                1, datetime(2020, 1, 1, tzinfo=timezone.utc),
                'original', 'Closes #100', ('a.c',)),
            self._commit(
                2, datetime(2020, 1, 2, tzinfo=timezone.utc),
                'intervening', 'Closes #101', ('other.c',)),
            self._commit(
                3, datetime(2020, 1, 3, tzinfo=timezone.utc),
                'follow-up part one',
                'This is a follow-up to PR #100.\n\nCloses #102',
                ('a.c',)),
            self._commit(
                4, datetime(2020, 1, 4, tzinfo=timezone.utc),
                'follow-up part two', 'Closes #102', ('b.c',)),
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(len(plan['groups']), 3)
        self.assertIsNone(plan['groups'][2]['commits'][0]['movement'])
        candidates = [
            item for item in plan['candidates']
            if item['commit'] == f'{3:040x}' and
            item['trigger'] == 'grouped-explicit-pr'
        ]
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates[0]['trigger'], 'grouped-explicit-pr')
        self.assertIn(
            'XXX-illumos: not moved (grouped-explicit-pr)',
            self.markdown.read_text())

    def test_marks_partial_revert_without_moving_it(self):
        original = f'{1:040x}'
        commits = [
            self._commit(
                1, datetime(2020, 1, 1, tzinfo=timezone.utc),
                'original', paths=('a.c',)),
            self._commit(
                2, datetime(2020, 1, 2, tzinfo=timezone.utc),
                'partial correction',
                f'Partially reverts commit {original}.',
                ('a.c',)),
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(len(plan['groups']), 2)
        candidate = next(
            item for item in plan['candidates']
            if item['trigger'] == 'partial-revert')
        self.assertEqual(candidate['commit'], f'{2:040x}')
        self.assertIn(
            'XXX-illumos: not moved (partial-revert)',
            self.markdown.read_text())

    def test_records_expired_reference_without_raw_same_file_output(self):
        original = f'{1:040x}'
        commits = [
            self._commit(
                1, datetime(2020, 1, 1, tzinfo=timezone.utc),
                'introduce behavior', paths=('a.c',)),
            self._commit(
                2, datetime(2020, 2, 1, tzinfo=timezone.utc),
                'unrelated a change', paths=('a.c',)),
            self._commit(
                3, datetime(2020, 8, 1, tzinfo=timezone.utc),
                'late correction',
                f'Fix regression introduced by commit {original}.',
                ('a.c',)),
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(len(plan['groups']), 3)
        candidates = {
            (item['commit'], item['trigger'])
            for item in plan['candidates']
        }
        self.assertNotIn((f'{2:040x}', 'next-same-file'), candidates)
        self.assertIn((f'{3:040x}', 'expired-explicit-commit'), candidates)
        markdown = self.markdown.read_text()
        self.assertNotIn('next-same-file', markdown)
        self.assertIn(
            'XXX-illumos: not moved (expired-explicit-commit)',
            markdown)

    def test_records_pass_one_wording_clues(self):
        messages = (
            ('planned work', 'Some follow up work is planned.', 'follow-up'),
            ('regression', 'This was introduced by an earlier change.',
             'introduced-by'),
            ('revert', 'This reverts commit deadbee.', 'revert'),
            ('reapply', 'Re-apply the change after the backout.',
             'reapply-backout'),
            ('replacement', 'The old interface has been superseded.',
             'supersede'),
            ('named fix', 'Including the header first fixes this problem.',
             'named-later-fix'),
        )
        commits = [
            self._commit(
                number,
                datetime(2020, 1, number, tzinfo=timezone.utc),
                subject, body, (f'{number}.c',))
            for number, (subject, body, _clue) in enumerate(messages, 1)
        ]
        self._write_log(commits)

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(plan['clue_commit_count'], len(messages))
        self.assertEqual(len(plan['clue_candidates']), len(messages))
        recorded = {
            item['subject']: item['clues']
            for group in plan['groups']
            for item in group['commits']
        }
        for subject, _body, clue in messages:
            self.assertIn(clue, recorded[subject])

    def test_preserves_manual_attach_and_separate_decisions(self):
        original = f'{1:040x}'
        commits = [
            self._commit(
                1, datetime(2020, 1, 1, tzinfo=timezone.utc),
                'first', paths=('a.c',)),
            self._commit(
                2, datetime(2020, 1, 2, tzinfo=timezone.utc),
                'second', paths=('a.c',)),
            self._commit(
                3, datetime(2020, 1, 3, tzinfo=timezone.utc),
                'third', f'This is a follow-up to commit {original}.',
                ('a.c',)),
        ]
        self._write_log(commits)
        result = self._run()
        self.assertEqual(result.returncode, 0, result.stderr)

        plan = self._load_plan()
        plan['decisions'] = [
            {
                'action': 'attach',
                'commit': f'{2:040x}',
                'target': f'{1:040x}',
                'reason': 'same logical change',
            },
            {
                'action': 'separate',
                'commit': f'{3:040x}',
                'target': f'{1:040x}',
                'reason': 'independent correction',
            },
        ]
        self.plan.write_text(
            json.dumps(plan, indent=2) + '\n', encoding='utf-8')

        result = self._run()

        self.assertEqual(result.returncode, 0, result.stderr)
        plan = self._load_plan()
        self.assertEqual(plan['decisions'][0]['reason'], 'same logical change')
        self.assertEqual(
            [group['commits'][0]['commit'] for group in plan['groups']],
            [f'{number:040x}' for number in (1, 2, 3)])
        self.assertEqual(
            plan['groups'][1]['commits'][0]['movement']['trigger'],
            'manual-attach')
        self.assertEqual(len(plan['groups']), 3)


if __name__ == '__main__':
    unittest.main()
