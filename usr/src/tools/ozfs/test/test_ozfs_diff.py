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
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parent.parent / 'ozfs-diff.py'
GROUPS = ('core', 'platform', 'userland', 'tests', 'exclude')


class OzfsDiffTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.template_tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-diff-template-', dir='/tmp')
        cls.addClassCleanup(cls.template_tempdir.cleanup)
        root = Path(cls.template_tempdir.name)
        cls.illumos_template = root / 'illumos'
        cls.openzfs_template = root / 'openzfs'
        cls._create_repo(cls.illumos_template, {
            'src/core/a.c': 'illumos a\n',
            'src/core/binary.dat': b'\0illumos\n',
            'src/core/nested/b.c': 'illumos b\n',
            'src/core/old_name.c': 'relocated content\n',
            'src/core/one/common.h': 'first illumos common\n',
            'src/core/one/resolved.h': 'identical resolved content\n',
            'src/core/two/common.h': 'second illumos common\n',
            'src/core/two/resolved.h': 'remaining illumos resolved\n',
            'src/user/u.c': 'illumos user\n',
            'src/excluded/e.c': 'illumos excluded\n',
        })
        cls._create_repo(cls.openzfs_template, {
            'module/core/a.c': 'openzfs a\n',
            'module/core/binary.dat': b'\0openzfs\n',
            'module/core/one/common.h': 'first OpenZFS common\n',
            'module/core/one/resolved.h': 'identical resolved content\n',
            'module/core/two/common.h': 'second OpenZFS common\n',
            'module/core/two/resolved.h': 'remaining OpenZFS resolved\n',
            'module/platform/new_name.c': 'relocated content\n',
            'module/platform/p.c': 'openzfs platform\n',
            'cmd/user/u.c': 'openzfs user\n',
            'tests/t.ksh': 'openzfs test\n',
            'build/Makefile.am': 'OpenZFS build metadata\n',
        })

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-diff-', dir='/tmp')
        self.root = Path(self.tempdir.name)
        self.illumos = self.root / 'illumos'
        self.openzfs = self.root / 'openzfs'
        self.lists = self.root / 'lists'
        self.config_dir = self.root / 'config'

        shutil.copytree(self.illumos_template, self.illumos)
        shutil.copytree(self.openzfs_template, self.openzfs)
        self._create_lists()
        self.config_dir.mkdir()

    def tearDown(self):
        self.tempdir.cleanup()

    @staticmethod
    def _git(repo, *args):
        return subprocess.run(
            ['git', '-C', str(repo), *args],
            check=True, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE).stdout.strip()

    @classmethod
    def _create_repo(cls, repo, files):
        repo.mkdir()
        cls._git(repo, 'init', '--quiet')
        cls._git(repo, 'config', 'user.name', 'ozfs-diff test')
        cls._git(repo, 'config', 'user.email', 'test@example.invalid')
        for name, contents in files.items():
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(contents, bytes):
                path.write_bytes(contents)
            else:
                path.write_text(contents, encoding='utf-8')
        cls._git(repo, 'add', '.')
        cls._git(repo, 'commit', '--quiet', '-m', 'test tree')

    def _create_lists(self):
        self.lists.mkdir()
        paths = {
            'core-illumos.txt': 'src/core\n',
            'core-openzfs.txt': 'module/core\n',
            'platform-illumos.txt': (
                'src/core/old_name.c\n'
                'src/core\n'
            ),
            'platform-openzfs.txt': (
                'module/platform/new_name.c\n'
                'module/platform\n'
            ),
            'userland-illumos.txt': 'src/user\n',
            'userland-openzfs.txt': 'cmd/user\n',
            'tests-illumos.txt': 'tests\n',
            'tests-openzfs.txt': 'tests\n',
            'addition-illumos.txt': '',
            'addition-openzfs.txt': '',
            'exclude-illumos.txt': 'src/excluded\n',
            'exclude-openzfs.txt': (
                'excluded\n'
                ':(glob)**/Makefile.am\n'
            ),
            'exception-illumos.txt': ':(glob)src/core/a.?\n',
            'exception-openzfs.txt': ':(glob)module/core/a.?\n',
        }
        for name, contents in paths.items():
            (self.lists / name).write_text(contents, encoding='utf-8')
        (self.lists / 'platform-mappings.tsv').write_text(
            'role\tvariant\tillumos-path\topenzfs-path\n'
            'primary\tlinux\tsrc/core/old_name.c\t'
            'module/platform/new_name.c\n'
            'primary\tlinux\tsrc/core/nested/b.c\t'
            'module/platform/p.c\n',
            encoding='utf-8')

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
        self.assertIn('core: illumos=8 OpenZFS=6', result.stdout)
        self.assertIn('platform: illumos=8 OpenZFS=2', result.stdout)
        self.assertIn('userland: illumos=1 OpenZFS=1', result.stdout)
        self.assertIn('tests: illumos=0 OpenZFS=1', result.stdout)
        self.assertIn('exclude: illumos=1 OpenZFS=1', result.stdout)
        self.assertIn(
            'core: path=7 ambiguous=0 '
            'illumos-unmatched=1 OpenZFS-unmatched=1',
            result.stdout)
        self.assertIn(
            'userland: path=1 ambiguous=0 '
            'illumos-unmatched=0 OpenZFS-unmatched=0',
            result.stdout)
        self.assertIn(
            'tests: path=0 ambiguous=0 '
            'illumos-unmatched=0 OpenZFS-unmatched=1',
            result.stdout)
        self.assertIn(
            'platform supplement: matched=1 unmatched=1',
            result.stdout)
        self.assertIn(
            'core: identical=2 text=3 binary=1 exceptional=1 '
            'added=3 deleted=3',
            result.stdout)
        self.assertIn(
            'userland: identical=0 text=1 binary=0 exceptional=0 '
            'added=1 deleted=1',
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
                'comparisons.tsv',
                'matches.tsv',
                'summary.txt',
                'unmatched-illumos.txt',
                'unmatched-openzfs.txt',
            ])
        matches = (output / 'matches.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'core\tpath\tplatform\tsrc/core/old_name.c\t'
            'module/platform/new_name.c\t',
            matches)
        a_match = next(
            line for line in matches.splitlines()
            if '\tsrc/core/a.c\tmodule/core/a.c\t' in line)
        self.assertTrue(a_match.endswith('\ttrue'), a_match)
        comparisons = (
            output / 'comparisons.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'core\ttext\ttrue\t1\t1\tsrc/core/a.c\t'
            'module/core/a.c\n',
            comparisons)
        self.assertIn(
            'core\tbinary\tfalse\t0\t0\tsrc/core/binary.dat\t'
            'module/core/binary.dat\n',
            comparisons)
        ambiguous = (output / 'ambiguous.tsv').read_text(encoding='utf-8')
        self.assertEqual(
            ambiguous,
            'group\tbasename\tside\tpath\toid\n')
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

    def test_rejects_unpaired_comparison_paths(self):
        config = self._write_config()
        with (self.lists / 'core-illumos.txt').open('a') as stream:
            stream.write('src/extra\n')

        result = self._run(config)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'path map length mismatch: core-illumos.txt has 2 entries; '
            'core-openzfs.txt has 1',
            result.stderr)

    def test_scopes_comparison_to_openzfs_diff(self):
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        changed = self.openzfs / 'module/core/two/resolved.h'
        changed.write_text(
            'remaining illumos resolved\n', encoding='utf-8')
        excluded = self.openzfs / 'excluded/e.c'
        excluded.parent.mkdir()
        excluded.write_text('excluded candidate change\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')
        output = self.root / 'scoped-report'

        result = self._run(
            config, '--ozb', base, '--out', str(output))

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'scope: changed=2 mapped=1 alternate=0 excluded=1',
            result.stdout)
        self.assertIn(
            'before: identical=0 text=1 binary=0 added=1 deleted=1',
            result.stdout)
        self.assertIn(
            'after: identical=1 text=0 binary=0 added=0 deleted=0',
            result.stdout)
        self.assertIn(
            'line distance: before=2 after=0 delta=-2',
            result.stdout)
        comparisons = (
            output / 'comparisons.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'core\ttext\tidentical\tfalse\t1\t1\t0\t0\t'
            'src/core/two/resolved.h\t'
            'module/core/two/resolved.h\n',
            comparisons)
        self.assertNotIn('binary.dat', comparisons)
        scope = (output / 'scope.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'excluded\t\texcluded/e.c\t\n',
            scope)

    def test_scoped_comparison_reports_exceptional_target(self):
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        (self.openzfs / 'module/core/a.c').write_text(
            'exceptional candidate change\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')
        output = self.root / 'exceptional-report'

        result = self._run(
            config, '--ozb', base, '--out', str(output))

        self.assertEqual(result.returncode, 0, result.stderr)
        comparisons = (
            output / 'comparisons.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'src/core/a.c\tmodule/core/a.c\n',
            comparisons)
        self.assertIn('\ttrue\t', comparisons)

    def test_scoped_comparison_transforms_platform_path(self):
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        (self.openzfs / 'module/platform/new_name.c').write_text(
            'changed platform content\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')
        output = self.root / 'platform-report'

        result = self._run(
            config, '--ozb', base, '--out', str(output))

        self.assertEqual(result.returncode, 0, result.stderr)
        comparisons = (
            output / 'comparisons.tsv').read_text(encoding='utf-8')
        self.assertIn(
            'src/core/old_name.c\tmodule/platform/new_name.c\n',
            comparisons)

    def test_scoped_comparison_rejects_unmapped_changed_path(self):
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        unmapped = self.openzfs / 'misc/new.c'
        unmapped.parent.mkdir()
        unmapped.write_text('unmapped\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')

        result = self._run(config, '--ozb', base)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unclassified path: misc/new.c', result.stderr)

    def test_scoped_comparison_handles_rename_as_add_and_delete(self):
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        self._git(
            self.openzfs, 'mv',
            'module/core/one/resolved.h',
            'module/core/one/renamed.h')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')

        result = self._run(config, '--ozb', base)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'scope: changed=2 mapped=2 alternate=0 excluded=0',
            result.stdout)
        self.assertIn(
            'before: identical=2 text=0 binary=0 added=0 deleted=0',
            result.stdout)
        self.assertIn(
            'after: identical=0 text=2 binary=0 added=1 deleted=1',
            result.stdout)

    def test_scoped_comparison_reports_alternate_platform_path(self):
        alternate = self.openzfs / 'module/platform/alternate.c'
        alternate.write_text('before\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'add alternate')
        with (self.lists / 'platform-mappings.tsv').open(
                'a', encoding='utf-8') as stream:
            stream.write(
                'alternate\tlinux\tsrc/core/old_name.c\t'
                'module/platform/alternate.c\n')
        config = self._write_config()
        base = self._git(self.openzfs, 'rev-parse', 'HEAD')
        alternate.write_text('after\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'candidate')

        result = self._run(config, '--ozb', base)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'scope: changed=1 mapped=0 alternate=1 excluded=0',
            result.stdout)


if __name__ == '__main__':
    unittest.main()
