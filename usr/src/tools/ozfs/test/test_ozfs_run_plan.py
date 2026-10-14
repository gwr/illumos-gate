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
from pathlib import Path


SCRIPT = Path(__file__).parent.parent / 'ozfs-run-plan.py'


class OzfsRunPlanTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-run-plan-', dir='/tmp')
        self.root = Path(self.tempdir.name)
        self.plan = self.root / 'plan.json'
        self.config = self.root / 'ozfs.conf'
        self.config.write_text('test config\n', encoding='utf-8')
        self.calls = self.root / 'calls.jsonl'
        self.apply = self.root / 'fake-apply.py'
        self.apply.write_text(
            (
                'import json\n'
                'import sys\n'
                'from pathlib import Path\n'
                f'path = Path({str(self.calls)!r})\n'
                'with path.open("a", encoding="utf-8") as stream:\n'
                '    stream.write(json.dumps(sys.argv[1:]) + "\\n")\n'
                'if "G0002" in sys.argv:\n'
                '    sys.exit(7)\n'
            ),
            encoding='utf-8')
        self.groups = [
            self._group('G0001', 1, 'first group', 1, 2),
            self._group('G0002', 3, 'second group', 3),
            self._group('G0003', 4, 'third group', 4),
        ]
        self._write_plan(self.groups)

    def tearDown(self):
        self.tempdir.cleanup()

    def _group(self, group_id, ordinal, name, *commits):
        return {
            'id': group_id,
            'original_ordinal': ordinal,
            'name': name,
            'moved_after': None,
            'todo': None,
            'commits': [
                {
                    'commit': f'{number:040x}',
                    'original_ordinal': number,
                }
                for number in commits
            ],
        }

    def _write_plan(self, groups, version=1):
        self.plan.write_text(
            json.dumps({'version': version, 'groups': groups}),
            encoding='utf-8')

    def _run(self, *arguments):
        return subprocess.run(
            [
                sys.executable, str(SCRIPT),
                '--plan', str(self.plan),
                '--ozfs-apply', str(self.apply),
                *arguments,
            ],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def _calls(self):
        if not self.calls.exists():
            return []
        return [
            json.loads(line)
            for line in self.calls.read_text(encoding='utf-8').splitlines()
        ]

    def test_lists_from_commit_inclusively_with_limit(self):
        start_commit = self.groups[1]['commits'][0]['commit']

        result = self._run(
            '--list', '--start', start_commit, '--limit', '1')

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Plan SHA-256:', result.stdout)
        self.assertIn('G0002', result.stdout)
        self.assertIn('second group', result.stdout)
        self.assertNotIn('G0001', result.stdout)
        self.assertNotIn('G0003', result.stdout)
        self.assertEqual(self._calls(), [])

    def test_runs_after_group_and_passes_commits_in_order(self):
        result = self._run(
            '--cf', str(self.config), '--after', 'G0001', '--limit', '1')

        self.assertEqual(result.returncode, 7)
        self.assertEqual(
            self._calls(),
            [[
                '--cf', str(self.config),
                '--group', 'G0002',
                self.groups[1]['commits'][0]['commit'],
            ]])
        self.assertIn('G0002 failed with exit status 7', result.stderr)
        self.assertIn('--start G0002', result.stderr)
        self.assertIn('--after G0002', result.stderr)
        self.assertIn(
            f'\n  {SCRIPT} --plan', result.stderr)
        self.assertNotIn(
            f'\n  {sys.executable} {SCRIPT}', result.stderr)

    def test_stops_after_first_failure(self):
        result = self._run('--cf', str(self.config))

        self.assertEqual(result.returncode, 7)
        calls = self._calls()
        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0][3], 'G0001')
        self.assertEqual(
            calls[0][4:],
            [item['commit'] for item in self.groups[0]['commits']])
        self.assertEqual(calls[1][3], 'G0002')

    def test_rejects_unknown_selector_before_running(self):
        result = self._run(
            '--cf', str(self.config), '--start', 'G9999')

        self.assertEqual(result.returncode, 1)
        self.assertIn('selector not found: G9999', result.stderr)
        self.assertEqual(self._calls(), [])

    def test_rejects_unsupported_plan_version(self):
        self._write_plan(self.groups, version=2)

        result = self._run('--list')

        self.assertEqual(result.returncode, 1)
        self.assertIn('unsupported plan version: 2', result.stderr)

    def test_requires_config_when_applying(self):
        result = self._run()

        self.assertEqual(result.returncode, 1)
        self.assertIn('--cf is required unless --list is used', result.stderr)
        self.assertEqual(self._calls(), [])


if __name__ == '__main__':
    unittest.main()
