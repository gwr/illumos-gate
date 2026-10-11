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

import configparser
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parent.parent / 'ozfs-diff.py'
GROUPS = ('core', 'platform', 'userland', 'tests', 'exclude')


class OzfsDiffTest(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.illumos = self.root / 'illumos'
        self.openzfs = self.root / 'openzfs'
        self.lists = self.root / 'lists'
        self.config_dir = self.root / 'config'

        self._create_repo(self.illumos, {
            'src/core/a.c': 'illumos a\n',
            'src/core/nested/b.c': 'illumos b\n',
            'src/core/old_name.c': 'relocated content\n',
            'src/core/one/common.h': 'first illumos common\n',
            'src/core/one/resolved.h': 'identical resolved content\n',
            'src/core/two/common.h': 'second illumos common\n',
            'src/core/two/resolved.h': 'remaining illumos resolved\n',
            'src/user/u.c': 'illumos user\n',
            'src/excluded/e.c': 'illumos excluded\n',
        })
        self._create_repo(self.openzfs, {
            'module/core/a.c': 'openzfs a\n',
            'module/core/one/common.h': 'first OpenZFS common\n',
            'module/core/one/resolved.h': 'identical resolved content\n',
            'module/core/two/common.h': 'second OpenZFS common\n',
            'module/core/two/resolved.h': 'remaining OpenZFS resolved\n',
            'module/platform/new_name.c': 'relocated content\n',
            'module/platform/p.c': 'openzfs platform\n',
            'cmd/user/u.c': 'openzfs user\n',
            'tests/t.ksh': 'openzfs test\n',
        })
        self._create_lists()
        self.config_dir.mkdir()

    def tearDown(self):
        self.tempdir.cleanup()

    def _git(self, repo, *args):
        return subprocess.run(
            ['git', '-C', str(repo), *args],
            check=True, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE).stdout.strip()

    def _create_repo(self, repo, files):
        repo.mkdir()
        self._git(repo, 'init', '--quiet')
        self._git(repo, 'config', 'user.name', 'ozfs-diff test')
        self._git(repo, 'config', 'user.email', 'test@example.invalid')
        for name, contents in files.items():
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        self._git(repo, 'add', '.')
        self._git(repo, 'commit', '--quiet', '-m', 'test tree')

    def _create_lists(self):
        self.lists.mkdir()
        paths = {
            'core-illumos.txt': 'src/core\n',
            'core-openzfs.txt': 'module/core\n',
            'platform-illumos.txt': '',
            'platform-openzfs.txt': 'module/platform\n',
            'userland-illumos.txt': 'src/user\n',
            'userland-openzfs.txt': 'cmd/user\n',
            'tests-illumos.txt': 'tests\n',
            'tests-openzfs.txt': 'tests\n',
            'exclude-illumos.txt': 'src/excluded\n',
            'exclude-openzfs.txt': 'excluded\n',
        }
        for name, contents in paths.items():
            (self.lists / name).write_text(contents, encoding='utf-8')

    def _write_config(
            self, *, include_illumos=True, extra=None, runtime_dir=None):
        if runtime_dir is None:
            runtime_dir = self.root
        config = configparser.ConfigParser(interpolation=None)
        config['repositories'] = {
            'openzfs': os.path.relpath(self.openzfs, runtime_dir),
        }
        if include_illumos:
            config['repositories']['illumos'] = os.path.relpath(
                self.illumos, runtime_dir)
        config['paths'] = {
            'lists': os.path.relpath(self.lists, runtime_dir),
        }
        if extra is not None:
            section, key, value = extra
            if section not in config:
                config[section] = {}
            config[section][key] = value

        path = self.config_dir / 'ozfs-diff.conf'
        with path.open('w', encoding='utf-8') as stream:
            config.write(stream)
        return path

    def _run(self, config, *args, cwd=None):
        if cwd is None:
            cwd = self.root
        return subprocess.run(
            [sys.executable, str(SCRIPT), '--cf', str(config),
             '--ozc', 'HEAD', '--ilc', 'HEAD', *args],
            cwd=cwd, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

    def test_reports_resolved_commits_and_group_counts(self):
        config = self._write_config()

        result = self._run(config)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            f'illumos commit: {self._git(self.illumos, "rev-parse", "HEAD")}',
            result.stdout)
        self.assertIn(
            f'OpenZFS commit: {self._git(self.openzfs, "rev-parse", "HEAD")}',
            result.stdout)
        self.assertIn('core: illumos=7 OpenZFS=5', result.stdout)
        self.assertIn('platform: illumos=0 OpenZFS=2', result.stdout)
        self.assertIn('userland: illumos=1 OpenZFS=1', result.stdout)
        self.assertIn('tests: illumos=0 OpenZFS=1', result.stdout)
        self.assertIn('exclude: illumos=1 OpenZFS=0', result.stdout)
        self.assertIn(
            'core: basename=2 content=2 ambiguous=1 '
            'illumos-unmatched=3 OpenZFS-unmatched=3',
            result.stdout)
        self.assertIn(
            'userland: basename=1 content=0 ambiguous=0 '
            'illumos-unmatched=0 OpenZFS-unmatched=0',
            result.stdout)
        self.assertIn(
            'tests: basename=0 content=0 ambiguous=0 '
            'illumos-unmatched=0 OpenZFS-unmatched=1',
            result.stdout)
        self.assertIn(
            'platform supplement: matched=1 unmatched=1',
            result.stdout)

    def test_defaults_illumos_repository_to_current_directory(self):
        config = self._write_config(
            include_illumos=False, runtime_dir=self.illumos)

        result = self._run(config, cwd=self.illumos)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            f'illumos repository: {self.illumos.resolve()}', result.stdout)

    def test_rejects_unknown_configuration_setting(self):
        config = self._write_config(
            extra=('repositories', 'unexpected', 'value'))

        result = self._run(config)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'unknown configuration setting: repositories.unexpected',
            result.stderr)

    def test_rejects_invalid_commit(self):
        config = self._write_config()

        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--cf', str(config),
             '--ozc', 'not-a-commit', '--ilc', 'HEAD'],
            cwd=self.root, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'OpenZFS commit is not a commit: not-a-commit', result.stderr)

    def test_rejects_missing_standard_path_list(self):
        config = self._write_config()
        (self.lists / 'tests-openzfs.txt').unlink()

        result = self._run(config)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'missing path list: tests-openzfs.txt', result.stderr)

    def test_writes_detailed_report_directory(self):
        config = self._write_config()
        output = self.root / 'report'

        result = self._run(config, '--out', str(output))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            sorted(path.name for path in output.iterdir()),
            [
                'ambiguous.tsv',
                'matches.tsv',
                'summary.txt',
                'unmatched-illumos.txt',
                'unmatched-openzfs.txt',
            ])
        matches = (output / 'matches.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'core\tcontent\tplatform\tsrc/core/old_name.c\t'
            'module/platform/new_name.c\t',
            matches)
        ambiguous = (output / 'ambiguous.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'core\tcommon.h\tillumos\tsrc/core/one/common.h\t',
            ambiguous)
        openzfs_unmatched = (
            output / 'unmatched-openzfs.txt').read_text(encoding='utf-8')
        self.assertIn(
            'core\tplatform\tmodule/platform/p.c\n',
            openzfs_unmatched)
        self.assertEqual(
            (output / 'summary.txt').read_text(encoding='utf-8'),
            result.stdout)

    def test_refuses_existing_output_directory(self):
        config = self._write_config()
        output = self.root / 'report'
        output.mkdir()

        result = self._run(config, '--out', str(output))

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            f'output path already exists: {output.resolve()}', result.stderr)


if __name__ == '__main__':
    unittest.main()
