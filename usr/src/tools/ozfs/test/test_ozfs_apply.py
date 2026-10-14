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

import configparser
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parent.parent / 'ozfs-apply.py'
GROUPS = ('core', 'platform', 'userland', 'tests', 'exclude', 'exception')


class OzfsApplyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.template_tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-apply-template-', dir='/tmp')
        cls.addClassCleanup(cls.template_tempdir.cleanup)
        root = Path(cls.template_tempdir.name)
        cls.illumos_template = root / 'illumos'
        cls.openzfs_template = root / 'openzfs'
        cls._create_repo(cls.illumos_template, {
            'usr/src/common/zfs/shared.h': 'shared\n',
            'usr/src/uts/common/fs/zfs/sys/kernel.h': 'kernel\n',
            'usr/src/uts/common/fs/zfs/main.c': 'main\n',
            'usr/src/uts/common/fs/zfs/platform.c': 'platform\n',
            'usr/src/uts/common/fs/zfs/exception.c': 'illumos exception\n',
        })
        cls._create_repo(cls.openzfs_template, {
            'include/sys/shared.h': 'shared\n',
            'include/sys/kernel.h': 'kernel\n',
            'module/zfs/main.c': 'main\n',
            'module/zfs/exception.c': 'OpenZFS exception\n',
            'module/os/freebsd/zfs/platform.c': 'FreeBSD platform\n',
            'module/os/linux/zfs/platform.c': 'Linux platform\n',
            'config/build.m4': 'config\n',
        })

    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-apply-', dir='/tmp')
        self.root = Path(self.tempdir.name)
        self.illumos = self.root / 'illumos'
        self.openzfs = self.root / 'openzfs'
        self.lists = self.root / 'lists'
        self.config = self.root / 'ozfs-diff.conf'

        shutil.copytree(self.illumos_template, self.illumos)
        shutil.copytree(self.openzfs_template, self.openzfs)
        self._create_lists()
        self._write_config()

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
        cls._git(repo, 'config', 'user.name', 'ozfs-apply test')
        cls._git(repo, 'config', 'user.email', 'test@example.invalid')
        for name, contents in files.items():
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        cls._git(repo, 'add', '.')
        cls._git(repo, 'commit', '--quiet', '-m', 'initial tree')

    def _create_lists(self):
        self.lists.mkdir()
        paths = {
            'core-illumos.txt': (
                'usr/src/common/zfs\n'
                'usr/src/uts/common/fs/zfs/sys\n'
                'usr/src/uts/common/fs/zfs\n'
            ),
            'core-openzfs.txt': (
                'include/sys\n'
                'include/sys\n'
                'module/zfs\n'
            ),
            'platform-illumos.txt': 'usr/src/uts/common/fs/zfs\n',
            'platform-openzfs.txt': 'module/os/linux/zfs\n',
            'userland-illumos.txt': '',
            'userland-openzfs.txt': '',
            'tests-illumos.txt': '',
            'tests-openzfs.txt': '',
            'addition-illumos.txt': '',
            'addition-openzfs.txt': '',
            'exclude-illumos.txt': '',
            'exclude-openzfs.txt': 'config\n',
            'exception-illumos.txt': (
                'usr/src/uts/common/fs/zfs/exception.c\n'
            ),
            'exception-openzfs.txt': 'module/zfs/exception.c\n',
        }
        for name, contents in paths.items():
            (self.lists / name).write_text(contents, encoding='utf-8')
        (self.lists / 'platform-mappings.tsv').write_text(
            'role\tvariant\tillumos-path\topenzfs-path\n'
            'primary\tfreebsd\tusr/src/uts/common/fs/zfs/platform.c\t'
            'module/os/freebsd/zfs/platform.c\n'
            'alternate\tlinux\tusr/src/uts/common/fs/zfs/platform.c\t'
            'module/os/linux/zfs/platform.c\n',
            encoding='utf-8')

    def _write_config(self):
        config = configparser.ConfigParser(interpolation=None)
        config['repositories'] = {
            'openzfs': os.path.relpath(self.openzfs, self.root),
            'illumos': os.path.relpath(self.illumos, self.root),
        }
        config['paths'] = {
            'lists': os.path.relpath(self.lists, self.root),
        }
        with self.config.open('w', encoding='utf-8') as stream:
            config.write(stream)

    def _approve_addition(self, source, target):
        with (self.lists / 'addition-openzfs.txt').open('a') as stream:
            stream.write(f'{source}\n')
        with (self.lists / 'addition-illumos.txt').open('a') as stream:
            stream.write(f'{target}\n')

    def _commit(self, message, changes, author=None, date=None):
        for name, contents in changes.items():
            path = self.openzfs / name
            if contents is None:
                path.unlink()
                continue
            path.parent.mkdir(parents=True, exist_ok=True)
            if isinstance(contents, bytes):
                path.write_bytes(contents)
            else:
                path.write_text(contents, encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        args = ['commit', '--quiet', '-m', message]
        if author is not None:
            args.append(f'--author={author}')
        if date is not None:
            args.append(f'--date={date}')
        self._git(self.openzfs, *args)
        return self._git(self.openzfs, 'rev-parse', 'HEAD')

    def _run(self, *args):
        return subprocess.run(
            [sys.executable, str(SCRIPT), '--cf', str(self.config), *args],
            cwd=self.root, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

    def test_check_reports_ordered_mapped_and_omitted_paths(self):
        first = self._commit('first change', {
            'module/zfs/main.c': 'main changed\n',
            'config/build.m4': 'config changed\n',
        })
        second = self._commit('second change', {
            'include/sys/kernel.h': 'kernel changed\n',
        })

        result = self._run('--check', first, second)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(result.stdout.index(first), result.stdout.index(second))
        self.assertIn(
            'mapped\tmodule/zfs/main.c\t'
            'usr/src/uts/common/fs/zfs/main.c',
            result.stdout)
        self.assertIn('excluded\tconfig/build.m4', result.stdout)
        self.assertIn(
            'mapped\tinclude/sys/kernel.h\t'
            'usr/src/uts/common/fs/zfs/sys/kernel.h',
            result.stdout)

    def test_configured_glob_excludes_path_beneath_mapped_root(self):
        with (self.lists / 'exclude-openzfs.txt').open('a') as stream:
            stream.write(':(glob)**/Makefile.am\n')
        commit = self._commit('build metadata', {
            'module/zfs/Makefile.am': 'build metadata\n',
        })

        result = self._run('--check', commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'excluded\tmodule/zfs/Makefile.am',
            result.stdout)

    def test_group_allows_commit_with_only_excluded_paths(self):
        excluded = self._commit('excluded metadata', {
            'config/build.m4': 'changed config\n',
        })
        mapped = self._commit('mapped source', {
            'module/zfs/main.c': 'changed source\n',
        })
        output = self.root / 'mixed-series.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001',
            excluded, mapped)

        self.assertEqual(result.returncode, 0, result.stderr)
        series = output.read_text(encoding='utf-8')
        self.assertIn(f'OpenZFS {excluded}', series)
        self.assertIn(f'OpenZFS {mapped}', series)
        self.assertNotIn('config/build.m4', series)
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/main.c '
            'b/usr/src/uts/common/fs/zfs/main.c',
            series)

    def test_completes_group_with_only_excluded_paths(self):
        commit = self._commit('excluded metadata', {
            'config/build.m4': 'changed config\n',
        })
        before = self._git(self.illumos, 'rev-parse', 'HEAD')

        result = self._run(
            '--group', 'G0001', commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'ozfs-apply: no mapped illumos paths; nothing to do',
            result.stdout)
        self.assertEqual(
            self._git(self.illumos, 'rev-parse', 'HEAD'), before)
        self.assertFalse((self.root / '.ozfs-manifest.tsv').exists())

    def test_output_writes_one_combined_reshaped_mail_patch(self):
        first = self._commit(
            'first change', {
                'module/zfs/main.c': 'main changed\n',
                'config/build.m4': 'config changed\n',
            },
            author='Primary Author <primary@example.com>',
            date='Thu, 2 Jan 2020 03:04:05 -0500')
        second = self._commit(
            'second change', {
                'include/sys/shared.h': 'shared changed\n',
            },
            author='Later Author <later@example.com>',
            date='Fri, 3 Jan 2020 04:05:06 -0500')
        output = self.root / 'series.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0040',
            first, second)

        self.assertEqual(result.returncode, 0, result.stderr)
        series = output.read_text(encoding='utf-8')
        self.assertEqual(
            sum(line.startswith('From ') for line in series.splitlines()), 1)
        self.assertTrue(series.startswith(
            f'From {first} Mon Sep 17 00:00:00 2001\n'
            'From: Primary Author <primary@example.com>\n'
            'Date: Thu, 2 Jan 2020 03:04:05 -0500\n'))
        self.assertIn(
            'Subject: [PATCH] first change', series)
        body = series.split('\n\n', 1)[1]
        self.assertTrue(body.startswith('Import G0040\n\n'))
        first_message = self._git(
            self.openzfs, 'show', '-s', '--format=%B', first)
        second_message = self._git(
            self.openzfs, 'show', '-s', '--format=%B', second)
        self.assertIn(
            f'OpenZFS {first}\n\n{first_message}\n\n'
            f'OpenZFS {second}\n\n{second_message}\n---\n',
            series)
        self.assertNotIn('import OpenZFS ', series)
        self.assertLess(series.index(first), series.index(second))
        self.assertIn('first change', series)
        self.assertIn('second change', series)
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/main.c '
            'b/usr/src/uts/common/fs/zfs/main.c',
            series)
        self.assertIn(
            'diff --git a/usr/src/common/zfs/shared.h '
            'b/usr/src/common/zfs/shared.h',
            series)
        self.assertNotIn('config/build.m4', series)

    def test_normalizes_commit_message_line_endings(self):
        path = self.openzfs / 'module/zfs/main.c'
        path.write_text('changed\n', encoding='utf-8')
        self._git(self.openzfs, 'add', 'module/zfs/main.c')
        message = self.root / 'message'
        message.write_bytes(
            b'line ending test\n\nfirst line\r\nsecond line\r\n')
        self._git(
            self.openzfs, 'commit', '--quiet', '--cleanup=verbatim',
            '-F', str(message))
        commit = self._git(self.openzfs, 'rev-parse', 'HEAD')
        output = self.root / 'line-endings.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001', commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        series = output.read_bytes()
        self.assertNotIn(b'\r', series)
        self.assertIn(b'first line\nsecond line\n', series)

    def test_maps_primary_platform_variant_and_reports_alternate(self):
        commit = self._commit('platform change', {
            'module/os/freebsd/zfs/platform.c': 'FreeBSD changed\n',
            'module/os/linux/zfs/platform.c': 'Linux changed\n',
        })
        output = self.root / 'platform.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001',
            commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'mapped\tmodule/os/freebsd/zfs/platform.c\t'
            'usr/src/uts/common/fs/zfs/platform.c',
            result.stdout)
        self.assertIn(
            'alternate\tmodule/os/linux/zfs/platform.c',
            result.stdout)
        series = output.read_text(encoding='utf-8')
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/platform.c '
            'b/usr/src/uts/common/fs/zfs/platform.c',
            series)
        self.assertNotIn('module/os/linux/zfs/platform.c', series)

    def test_refuses_mapping_to_excluded_illumos_path(self):
        (self.lists / 'exclude-illumos.txt').write_text(
            'usr/src/uts/common/fs/zfs/main.c\n', encoding='utf-8')
        commit = self._commit('excluded target change', {
            'module/zfs/main.c': 'changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'mapped illumos path is excluded: module/zfs/main.c -> '
            'usr/src/uts/common/fs/zfs/main.c',
            result.stderr)

    def test_refuses_platform_mapping_to_excluded_illumos_path(self):
        (self.lists / 'exclude-illumos.txt').write_text(
            'usr/src/uts/common/fs/zfs/platform.c\n', encoding='utf-8')
        commit = self._commit('excluded platform target change', {
            'module/os/freebsd/zfs/platform.c': 'changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'mapped illumos path is excluded: '
            'module/os/freebsd/zfs/platform.c -> '
            'usr/src/uts/common/fs/zfs/platform.c',
            result.stderr)

    def test_refuses_mapping_to_exceptional_illumos_path(self):
        (self.lists / 'exception-illumos.txt').write_text(
            ':(glob)**/main.c\n', encoding='utf-8')
        commit = self._commit('exceptional target change', {
            'module/zfs/main.c': 'changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/zfs/main.c\t'
            'usr/src/uts/common/fs/zfs/main.c\t'
            'exceptional illumos target',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_refuses_platform_mapping_to_exceptional_illumos_path(self):
        (self.lists / 'exception-illumos.txt').write_text(
            'usr/src/uts/common/fs/zfs/platform.c\n', encoding='utf-8')
        commit = self._commit('exceptional platform target change', {
            'module/os/freebsd/zfs/platform.c': 'changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/os/freebsd/zfs/platform.c\t'
            'usr/src/uts/common/fs/zfs/platform.c\t'
            'exceptional illumos target',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_refuses_modified_source_with_missing_mapped_target(self):
        target = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        target.unlink()
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m', 'remove mapped target')
        commit = self._commit('source change', {
            'module/zfs/main.c': 'changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/zfs/main.c\t'
            'usr/src/uts/common/fs/zfs/main.c\t'
            'mapped illumos target does not exist',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_stops_before_git_am_for_unapproved_mapped_addition(self):
        commit = self._commit('new source', {
            'module/zfs/new.c': 'new\n',
        })

        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review required for 1 path', result.stderr)
        self.assertIn(
            'git am was not run', result.stderr)
        patch = self.root / '.ozfs-review.mbox'
        self.assertTrue(patch.is_file())
        series = patch.read_text(encoding='utf-8')
        self.assertIn(
            'usr/src/uts/common/fs/zfs/new.c', series)
        self.assertIn('+new', series)
        self.assertEqual(
            self._git(self.illumos, 'rev-list', '--count', 'HEAD'), '1')
        self.assertFalse(
            (self.illumos / 'usr/src/uts/common/fs/zfs/new.c').exists())
        manifest = self.root / '.ozfs-manifest.tsv'
        self.assertTrue(manifest.is_file())
        self.assertIn(
            'review\tmodule/zfs/new.c\t'
            'usr/src/uts/common/fs/zfs/new.c\tnew illumos path\n',
            manifest.read_text(encoding='utf-8'))
        git_path = self._git(
            self.illumos, 'rev-parse', '--git-path', 'rebase-apply')
        operation = Path(git_path)
        if not operation.is_absolute():
            operation = self.illumos / operation
        self.assertFalse(operation.exists())

    def test_check_reports_unapproved_mapped_addition(self):
        commit = self._commit('new source', {
            'module/zfs/new.c': 'new\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/zfs/new.c\t'
            'usr/src/uts/common/fs/zfs/new.c\tnew illumos path\n',
            result.stdout)
        self.assertIn(
            'review required for 1 path', result.stderr)
        self.assertFalse((self.root / '.ozfs-review.mbox').exists())
        self.assertFalse((self.root / '.ozfs-manifest.tsv').exists())

    def test_review_patch_copies_exceptional_path_preimage(self):
        commit = self._commit('exceptional source change', {
            'module/zfs/exception.c': 'changed exception\n',
        })
        preimage = self._git(
            self.openzfs, 'rev-parse',
            f'{commit}^:module/zfs/exception.c')
        before = subprocess.run(
            ['git', '-C', str(self.illumos),
             'cat-file', '-e', preimage],
            check=False)
        self.assertNotEqual(before.returncode, 0)

        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertTrue((self.root / '.ozfs-review.mbox').is_file())
        self._git(self.illumos, 'cat-file', '-e', preimage)
        manifest = (
            self.root / '.ozfs-manifest.tsv'
        ).read_text(encoding='utf-8')
        self.assertIn('copying\tpreimage blobs\n', manifest)
        self.assertIn('copied\tpreimage blobs\n', manifest)

    def test_refuses_unselected_platform_interface(self):
        commit = self._commit('unknown platform change', {
            'module/os/freebsd/zfs/unknown.c': 'unknown\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/os/freebsd/zfs/unknown.c\t'
            'module/os/freebsd/zfs/unknown.c\tunclassified path',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_reshapes_addition_deletion_and_rename(self):
        self._approve_addition(
            'module/zfs/new.c',
            'usr/src/uts/common/fs/zfs/new.c')
        self._approve_addition(
            'module/zfs/renamed.c',
            'usr/src/uts/common/fs/zfs/renamed.c')
        first = self._commit('add and delete', {
            'module/zfs/main.c': None,
            'module/zfs/new.c': 'new\n',
        })
        self._git(
            self.openzfs, 'mv', 'module/zfs/new.c',
            'module/zfs/renamed.c')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'rename file')
        second = self._git(self.openzfs, 'rev-parse', 'HEAD')
        output = self.root / 'file-operations.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001',
            first, second)

        self.assertEqual(result.returncode, 0, result.stderr)
        series = output.read_text(encoding='utf-8')
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/main.c '
            'b/usr/src/uts/common/fs/zfs/main.c',
            series)
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/new.c '
            'b/usr/src/uts/common/fs/zfs/new.c',
            series)
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/renamed.c '
            'b/usr/src/uts/common/fs/zfs/renamed.c',
            series)
        self.assertNotIn('rename from', series)

    def test_preserves_binary_patch_and_mode_change(self):
        self._approve_addition(
            'module/zfs/data.bin',
            'usr/src/uts/common/fs/zfs/data.bin')
        first = self._commit('add binary file', {
            'module/zfs/data.bin': b'\0binary data\n',
        })
        path = self.openzfs / 'module/zfs/main.c'
        path.chmod(0o755)
        self._git(self.openzfs, 'add', 'module/zfs/main.c')
        self._git(self.openzfs, 'commit', '--quiet', '-m', 'change mode')
        second = self._git(self.openzfs, 'rev-parse', 'HEAD')
        output = self.root / 'binary-mode.mbox'

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001',
            first, second)

        self.assertEqual(result.returncode, 0, result.stderr)
        series = output.read_text(encoding='utf-8')
        self.assertIn('GIT binary patch', series)
        self.assertIn('old mode 100644', series)
        self.assertIn('new mode 100755', series)
        self.assertNotIn('module/zfs/data.bin', series)
        self.assertIn(
            'usr/src/uts/common/fs/zfs/data.bin',
            series)

    def test_applies_group_as_one_commit(self):
        first = self._commit('first applicable change', {
            'module/zfs/main.c': 'main changed once\n',
        })
        second = self._commit('second applicable change', {
            'module/zfs/main.c': 'main changed twice\n',
        })
        result = self._run(
            '--group', 'G0001',
            first, second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            (self.illumos / 'usr/src/uts/common/fs/zfs/main.c').read_text(
                encoding='utf-8'),
            'main changed twice\n')
        self.assertEqual(
            self._git(
                self.illumos, 'show', '-s', '--format=%s', 'HEAD'),
            'first applicable change')
        message = self._git(
            self.illumos, 'show', '-s', '--format=%B', 'HEAD')
        self.assertTrue(message.startswith(
            'first applicable change\n\n'
            'Import G0001\n\n'))
        self.assertIn(f'OpenZFS {first}', message)
        self.assertIn(f'OpenZFS {second}', message)
        self.assertEqual(
            self._git(
                self.illumos, 'rev-list', '--count', 'HEAD'), '2')
        self.assertFalse((self.root / '.ozfs-manifest.tsv').exists())

    def test_refuses_merge_commit(self):
        base_branch = self._git(
            self.openzfs, 'symbolic-ref', '--short', 'HEAD')
        self._git(self.openzfs, 'checkout', '-q', '-b', 'side')
        self._commit('side change', {
            'module/zfs/side.c': 'side\n',
        })
        self._git(self.openzfs, 'checkout', '-q', base_branch)
        self._commit('main change', {
            'module/zfs/main.c': 'main branch\n',
        })
        self._git(
            self.openzfs, 'merge', '--quiet', '--no-ff', 'side',
            '-m', 'merge side')
        commit = self._git(self.openzfs, 'rev-parse', 'HEAD')

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            f'merge commit requires mainline selection: {commit}',
            result.stderr)

    def test_refuses_invalid_commit(self):
        result = self._run('--check', 'not-a-commit')

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'OpenZFS commit is not a commit: not-a-commit',
            result.stderr)

    def test_refuses_existing_output_file(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        output = self.root / 'existing.mbox'
        output.write_text('keep me\n', encoding='utf-8')

        result = self._run(
            '--no-apply', '--output', str(output),
            '--group', 'G0001',
            commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            f'output path already exists: {output.resolve()}',
            result.stderr)
        self.assertEqual(output.read_text(encoding='utf-8'), 'keep me\n')

    def test_refuses_exceptional_path(self):
        commit = self._commit('exceptional change', {
            'module/zfs/exception.c': 'exception changed\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/zfs/exception.c\tmodule/zfs/exception.c\t'
            'exceptional OpenZFS path',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_refuses_unclassified_path(self):
        commit = self._commit('unclassified change', {
            'README.md': 'new documentation\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tREADME.md\tREADME.md\tunclassified path',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_refuses_ambiguous_new_path(self):
        commit = self._commit('ambiguous addition', {
            'include/sys/new.h': 'new header\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'ambiguous mapping for include/sys/new.h',
            result.stdout)
        self.assertIn('review required for 1 path', result.stderr)

    def test_requires_group_for_apply(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })

        result = self._run(commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--group is required when applying', result.stderr)

    def test_rejects_invalid_group(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })

        result = self._run(
            '--no-apply', '--output', str(self.root / 'invalid.mbox'),
            '--group', '40', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            '--group must be a GNNNN identifier', result.stderr)

    def test_no_apply_requires_output(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })

        result = self._run(
            '--no-apply', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--output is required with --no-apply', result.stderr)

    def test_refuses_dirty_illumos_worktree_before_apply(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        path = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        path.write_text('dirty illumos change\n', encoding='utf-8')

        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('illumos worktree or index is not clean', result.stderr)
        self.assertFalse((self.root / '.ozfs-manifest.tsv').exists())

    def test_git_am_conflict_hands_control_to_user(self):
        local = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        local.write_text('illumos local change\n', encoding='utf-8')
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m', 'local divergence')
        commit = self._commit('conflicting change', {
            'module/zfs/main.c': 'OpenZFS conflicting change\n',
        })
        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git am stopped; resolve it with Git', result.stderr)
        git_path = self._git(
            self.illumos, 'rev-parse', '--git-path', 'rebase-apply')
        operation = Path(git_path)
        if not operation.is_absolute():
            operation = self.illumos / operation
        self.assertTrue(operation.is_dir())
        manifest = self.root / '.ozfs-manifest.tsv'
        self.assertTrue(manifest.is_file())
        self.assertIn(commit, manifest.read_text(encoding='utf-8'))
        current = subprocess.run(
            [
                'git', '-C', str(self.illumos),
                'am', '--show-current-patch=raw',
            ],
            check=True, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE).stdout
        self.assertIn('Subject: [PATCH] conflicting change', current)

    def test_refuses_to_start_during_git_am(self):
        commit = self._commit('conflicting change', {
            'module/zfs/main.c': 'OpenZFS conflicting change\n',
        })
        patch = self.root / 'active.mbox'
        generated = self._run(
            '--no-apply', '--output', str(patch),
            '--group', 'G0001', commit)
        self.assertEqual(generated.returncode, 0, generated.stderr)
        local = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        local.write_text('illumos local change\n', encoding='utf-8')
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m', 'local divergence')
        subprocess.run(
            ['git', '-C', str(self.illumos), 'am', '--3way', str(patch)],
            check=False, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git am operation is already in progress', result.stderr)

    def test_refuses_existing_well_known_manifest(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        manifest = self.root / '.ozfs-manifest.tsv'
        manifest.write_text('previous operation\n', encoding='utf-8')

        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            f'output path already exists: {manifest.resolve()}',
            result.stderr)
        self.assertEqual(
            manifest.read_text(encoding='utf-8'),
            'previous operation\n')

    def test_review_stop_writes_complete_patch_and_manifest(self):
        commit = self._commit('unclassified change', {
            'README.md': 'new documentation\n',
            'module/zfs/main.c': 'mapped change\n',
        })

        result = self._run(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('review required for 1 path', result.stderr)
        patch = self.root / '.ozfs-review.mbox'
        self.assertTrue(patch.is_file())
        contents = patch.read_text(encoding='utf-8')
        self.assertIn(
            'diff --git a/README.md b/README.md', contents)
        self.assertIn(
            'diff --git a/usr/src/uts/common/fs/zfs/main.c '
            'b/usr/src/uts/common/fs/zfs/main.c',
            contents)
        manifest = self.root / '.ozfs-manifest.tsv'
        self.assertTrue(manifest.is_file())
        contents = manifest.read_text(encoding='utf-8')
        self.assertIn('group\tG0001\n', contents)
        self.assertIn(f'planning\t{commit}\n', contents)
        self.assertIn(
            'review\tREADME.md\tREADME.md\tunclassified path\n',
            contents)


if __name__ == '__main__':
    unittest.main()
