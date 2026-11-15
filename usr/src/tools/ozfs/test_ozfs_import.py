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

import configparser
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).resolve().parent / 'ozfs-import.py'
PRODUCTION_POLICY = SCRIPT.parent / 'import-paths.conf'


class OzfsImportTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.template_tempdir = tempfile.TemporaryDirectory(
            prefix='ozfs-import-template-', dir='/tmp')
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
            prefix='ozfs-import-', dir='/tmp')
        self.root = Path(self.tempdir.name)
        self.illumos = self.root / 'illumos'
        self.openzfs = self.root / 'openzfs'
        self.policy = self.root / 'import-paths.conf'
        self.config = self.root / 'ozfs-diff.conf'

        shutil.copytree(self.illumos_template, self.illumos)
        shutil.copytree(self.openzfs_template, self.openzfs)
        self._create_policy()
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
        cls._git(repo, 'config', 'user.name', 'ozfs-import test')
        cls._git(repo, 'config', 'user.email', 'test@example.invalid')
        for name, contents in files.items():
            path = repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        cls._git(repo, 'add', '.')
        cls._git(repo, 'commit', '--quiet', '-m', 'initial tree')

    @staticmethod
    def _policy_parser():
        config = configparser.ConfigParser(
            interpolation=None, allow_no_value=True, delimiters=('=',))
        config.optionxform = str
        return config

    def _create_policy(self):
        self.policy.write_text("""[map]
include/sys = usr/src/common/zfs
    usr/src/uts/common/fs/zfs/sys
module/zfs = usr/src/uts/common/fs/zfs

[additions]

[exclude-openzfs]
config

[exclude-illumos]

[review-illumos]
usr/src/uts/common/fs/zfs/exception.c

[review-openzfs]
module/zfs/exception.c

[primary-freebsd]
module/os/freebsd/zfs/platform.c = usr/src/uts/common/fs/zfs/platform.c

[alternate-linux]
module/os/linux/zfs/platform.c = usr/src/uts/common/fs/zfs/platform.c
""", encoding='utf-8')

    def _set_policy(self, section, source, target=None):
        config = self._policy_parser()
        config.read(self.policy)
        config[section][source] = target
        with self.policy.open('w') as stream:
            config.write(stream)

    def _write_config(self, policy=None):
        if policy is None:
            policy = self.policy
        config = configparser.ConfigParser(interpolation=None)
        config['repositories'] = {
            'openzfs': os.path.relpath(self.openzfs, self.root),
            'illumos': os.path.relpath(self.illumos, self.root),
        }
        config['paths'] = {'import-paths': str(Path(policy).resolve())}
        with self.config.open('w', encoding='utf-8') as stream:
            config.write(stream)

    def _approve_addition(self, source, target):
        self._set_policy('additions', source, target)

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
            [sys.executable, str(SCRIPT), '--config', str(self.config), *args],
            cwd=self.root, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)

    def _import(self, *args):
        # Exercise the public two-step interface for the existing import cases.
        count = getattr(self, 'import_count', 0) + 1
        self.import_count = count
        self.import_patch = self.root / f'import-{count}.patch'
        prepared = self._run('--output', str(self.import_patch), *args)
        if prepared.returncode or not self.import_patch.exists():
            return prepared
        return self._run('--apply', str(self.import_patch))

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
        self._set_policy('exclude-openzfs', ':(glob)**/Makefile.am')
        commit = self._commit('build metadata', {
            'module/zfs/Makefile.am': 'build metadata\n',
        })

        result = self._run('--check', commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'excluded\tmodule/zfs/Makefile.am',
            result.stdout)

    def test_production_userland_native_copy_requires_review(self):
        target = self.illumos / 'usr/src/lib/libnvpair/libnvpair_json.c'
        target.parent.mkdir(parents=True)
        target.write_text('native copy\n', encoding='utf-8')
        header = self.illumos / 'usr/src/lib/libnvpair/libnvpair.h'
        header.write_text('native header\n', encoding='utf-8')
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m',
            'add native userland copy')

        source = self.openzfs / 'lib/libnvpair/libnvpair_json.c'
        source.parent.mkdir(parents=True)
        source.write_text('upstream copy\n', encoding='utf-8')
        upstream_header = self.openzfs / 'include/libnvpair.h'
        upstream_header.parent.mkdir(parents=True, exist_ok=True)
        upstream_header.write_text('upstream header\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(
            self.openzfs, 'commit', '--quiet', '-m',
            'add upstream userland copy')

        self._write_config(PRODUCTION_POLICY)
        commit = self._commit('change native userland copy', {
            'lib/libnvpair/libnvpair_json.c': 'upstream changed\n',
            'include/libnvpair.h': 'upstream header changed\n',
            'lib/libnvpair/libnvpair.abi': 'metadata\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tlib/libnvpair/libnvpair_json.c\t'
            'usr/src/lib/libnvpair/libnvpair_json.c\t'
            'exceptional illumos target',
            result.stdout, result.stderr)
        self.assertIn(
            'review\tinclude/libnvpair.h\t'
            'usr/src/lib/libnvpair/libnvpair.h\t'
            'exceptional illumos target',
            result.stdout)
        self.assertIn(
            'excluded\tlib/libnvpair/libnvpair.abi',
            result.stdout)

    def test_production_core_native_copy_requires_review(self):
        target = self.illumos / 'usr/src/common/avl/avl.c'
        target.parent.mkdir(parents=True)
        target.write_text('native copy\n', encoding='utf-8')
        header = self.illumos / 'usr/src/uts/common/sys/avl.h'
        header.parent.mkdir(parents=True)
        header.write_text('native header\n', encoding='utf-8')
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m',
            'add native core copy')

        source = self.openzfs / 'module/avl/avl.c'
        source.parent.mkdir(parents=True)
        source.write_text('upstream copy\n', encoding='utf-8')
        upstream_header = self.openzfs / 'include/sys/avl.h'
        upstream_header.write_text('upstream header\n', encoding='utf-8')
        self._git(self.openzfs, 'add', '.')
        self._git(
            self.openzfs, 'commit', '--quiet', '-m',
            'add upstream core copy')

        self._write_config(PRODUCTION_POLICY)
        commit = self._commit('change native core copy', {
            'module/avl/avl.c': 'upstream changed\n',
            'include/sys/avl.h': 'upstream header changed\n',
            'module/avl/Makefile.in': 'metadata\n',
        })

        result = self._run('--check', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            'review\tmodule/avl/avl.c\t'
            'usr/src/common/avl/avl.c\t'
            'exceptional illumos target',
            result.stdout, result.stderr)
        self.assertIn(
            'review\tinclude/sys/avl.h\t'
            'usr/src/uts/common/sys/avl.h\t'
            'exceptional illumos target',
            result.stdout)
        self.assertIn(
            'excluded\tmodule/avl/Makefile.in',
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
            '--output', str(output),
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

        result = self._import(
            '--group', 'G0001', commit)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(
            'ozfs-import: no mapped illumos paths; nothing to do',
            result.stdout)
        self.assertEqual(
            self._git(self.illumos, 'rev-parse', 'HEAD'), before)
        self.assertFalse((self.illumos / '.git/ozfs-import.manifest.tsv').exists())

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
            '--output', str(output),
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
            '--output', str(output),
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
            '--output', str(output),
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
        self._set_policy('exclude-illumos',
                         'usr/src/uts/common/fs/zfs/main.c')
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
        self._set_policy('exclude-illumos',
                         'usr/src/uts/common/fs/zfs/platform.c')
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
        self._set_policy('review-illumos',
                         ':(glob)**/main.c')
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
        self._set_policy('review-illumos',
                         'usr/src/uts/common/fs/zfs/platform.c')
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

    def test_preparation_stops_for_unapproved_addition(self):
        commit = self._commit('new source', {'module/zfs/new.c': 'new\n'})
        patch = self.root / 'review.patch'
        result = self._run('--output', str(patch), commit)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('review required for 1 path', result.stderr)
        self.assertTrue(patch.is_file())
        self.assertIn('+new', patch.read_text())
        manifest = Path(str(patch) + '.manifest.tsv')
        self.assertIn('new illumos path', manifest.read_text())
        self.assertEqual(self._git(self.illumos, 'rev-list', '--count', 'HEAD'),
                         '1')
        self.assertFalse((self.illumos / '.git/rebase-apply').exists())

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
        self.assertFalse((self.illumos / '.git/ozfs-import-review.mbox').exists())
        self.assertFalse((self.illumos / '.git/ozfs-import.manifest.tsv').exists())

    def test_apply_reviewed_patch_copies_exceptional_preimage(self):
        commit = self._commit('exceptional source change', {
            'module/zfs/exception.c': 'changed exception\n',
        })
        preimage = self._git(self.openzfs, 'rev-parse',
                             f'{commit}^:module/zfs/exception.c')
        patch = self.root / 'reviewed.patch'
        prepared = self._run('--output', str(patch), commit)
        self.assertNotEqual(prepared.returncode, 0)
        text = patch.read_text().replace(
            'a/module/zfs/exception.c',
            'a/usr/src/uts/common/fs/zfs/exception.c').replace(
            'b/module/zfs/exception.c',
            'b/usr/src/uts/common/fs/zfs/exception.c')
        patch.write_text(text)
        result = self._run('--apply', str(patch))
        # The native content differs: Git owns the resulting merge conflict.
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git am stopped', result.stderr)
        self._git(self.illumos, 'cat-file', '-e', preimage)
        self.assertTrue((self.illumos / '.git/rebase-apply').exists())

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
            '--output', str(output),
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
            '--output', str(output),
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
        result = self._import(
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
        self.assertFalse((self.illumos / '.git/ozfs-import.manifest.tsv').exists())

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
            '--output', str(output),
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

    def test_default_output_uses_git_sanitized_first_subject(self):
        first = self._commit('Fix: ordinary / change!', {
            'module/zfs/main.c': 'first change\n',
        })
        second = self._commit('later follow-up', {
            'module/zfs/main.c': 'second change\n',
        })
        subject = self._git(self.openzfs, 'show', '-s', '--format=%f', first)
        output = self.root / f'0001-{subject}.patch'
        before = self._git(self.illumos, 'rev-parse', 'HEAD')
        result = self._run(first, second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(output.is_file())
        self.assertTrue(Path(str(output) + '.manifest.tsv').is_file())
        self.assertIn(f'OpenZFS {second}', output.read_text())
        self.assertEqual(self._git(self.illumos, 'rev-parse', 'HEAD'), before)
        original = output.read_bytes()
        repeated = self._run(first, second)
        self.assertNotEqual(repeated.returncode, 0)
        self.assertIn('output path already exists', repeated.stderr)
        self.assertEqual(output.read_bytes(), original)

    def test_default_filename_length_is_bounded(self):
        commit = self._commit('x' * 100, {
            'module/zfs/main.c': 'changed\n',
        })
        result = self._run(commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        output = self.root / ('0001-' + 'x' * 53 + '.patch')
        self.assertTrue(output.is_file())
        self.assertEqual(len(output.name), 64)

    def test_rejects_invalid_group(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })

        result = self._run(
            '--output', str(self.root / 'invalid.mbox'),
            '--group', 'bad\nlabel', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            '--group must be a nonempty single-line label', result.stderr)

    def test_apply_and_output_are_exclusive(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })

        result = self._run(
            '--apply', '--output', str(self.root / 'unused.mbox'), commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--output cannot be used with --check or --apply',
                      result.stderr)

    def test_refuses_dirty_illumos_worktree_before_apply(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        path = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        path.write_text('dirty illumos change\n', encoding='utf-8')

        result = self._import(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('illumos worktree or index is not clean', result.stderr)
        self.assertFalse((self.illumos / '.git/ozfs-import.manifest.tsv').exists())

    def test_git_am_conflict_hands_control_to_user(self):
        local = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        local.write_text('illumos local change\n', encoding='utf-8')
        self._git(self.illumos, 'add', '.')
        self._git(
            self.illumos, 'commit', '--quiet', '-m', 'local divergence')
        commit = self._commit('conflicting change', {
            'module/zfs/main.c': 'OpenZFS conflicting change\n',
        })
        result = self._import(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git am stopped; resolve it with Git', result.stderr)
        git_path = self._git(
            self.illumos, 'rev-parse', '--git-path', 'rebase-apply')
        operation = Path(git_path)
        if not operation.is_absolute():
            operation = self.illumos / operation
        self.assertTrue(operation.is_dir())
        manifest = self.illumos / '.git/ozfs-import.manifest.tsv'
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
            '--output', str(patch),
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
        result = self._import(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn('git am operation is already in progress', result.stderr)

    def test_refuses_existing_well_known_manifest(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        manifest = self.illumos / '.git/ozfs-import.manifest.tsv'
        manifest.write_text('previous operation\n', encoding='utf-8')

        result = self._import(
            '--group', 'G0001', commit)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            f'output path already exists: {manifest.resolve()}',
            result.stderr)
        self.assertEqual(
            manifest.read_text(encoding='utf-8'),
            'previous operation\n')

    def test_review_stop_writes_complete_patch_and_manifest(self):
        commit = self._commit('mixed review change', {
            'module/zfs/main.c': 'changed main\n',
            'README.md': 'review me\n',
        })
        patch = self.root / 'review.patch'
        result = self._run('--output', str(patch), '--group', 'G0001', commit)
        self.assertNotEqual(result.returncode, 0)
        contents = patch.read_text()
        self.assertIn('usr/src/uts/common/fs/zfs/main.c', contents)
        self.assertIn('README.md', contents)
        self.assertIn('unclassified path',
                      Path(str(patch) + '.manifest.tsv').read_text())
        self.assertFalse((self.illumos / '.git/rebase-apply').exists())

    def test_prepares_without_group_and_does_not_apply(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        before = self._git(self.illumos, 'rev-parse', 'HEAD')
        output = self.root / 'prepared.mbox'
        result = self._run('--output', str(output), commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self._git(self.illumos, 'rev-parse', 'HEAD'), before)
        self.assertIn(f'OpenZFS {commit}', output.read_text())
        self.assertNotIn('Import None', output.read_text())
        self._git(self.illumos, 'am', '--3way', str(output))
        self.assertEqual((self.illumos /
                          'usr/src/uts/common/fs/zfs/main.c').read_text(),
                         'main changed\n')

    def test_apply_accepts_no_group(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        result = self._import(commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(f'OpenZFS {commit}', self._git(
            self.illumos, 'show', '-s', '--format=%B', 'HEAD'))

    def test_preparation_returns_failure_with_review_artifacts(self):
        commit = self._commit('new file needs review', {
            'module/zfs/new.c': 'new file\n',
        })
        before = self._git(self.illumos, 'rev-parse', 'HEAD')
        output = self.root / 'review.mbox'
        result = self._run('--output', str(output), commit)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('review required', result.stderr)
        self.assertTrue(output.is_file())
        self.assertTrue(Path(str(output) + '.manifest.tsv').is_file())
        self.assertEqual(self._git(self.illumos, 'rev-parse', 'HEAD'), before)

    def test_config_paths_are_relative_to_config_from_another_cwd(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--config', str(self.config),
             '--check', commit], cwd=self.illumos, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_cli_overrides_and_current_worktree_default(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        result = subprocess.run(
            [sys.executable, str(SCRIPT), '--source', str(self.openzfs),
             '--import-paths', str(self.policy), '--check', commit],
            cwd=self.illumos / 'usr/src', text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_check_does_not_copy_preimages(self):
        commit = self._commit('exceptional change', {
            'module/zfs/exception.c': 'changed\n',
        })
        preimage = self._git(self.openzfs, 'rev-parse',
                             f'{commit}^:module/zfs/exception.c')
        result = self._run('--check', commit)
        self.assertNotEqual(result.returncode, 0)
        probe = subprocess.run(
            ['git', '-C', str(self.illumos), 'cat-file', '-e', preimage],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(probe.returncode, 0)

    def test_only_apply_copies_preimages_for_three_way_merge(self):
        lines = [f'line {n}\n' for n in range(30)]
        original = ''.join(lines)
        self._commit('source baseline', {'module/zfs/main.c': original})
        target = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        native = lines.copy()
        native[12] = 'native middle\n'
        target.write_text(''.join(native))
        self._git(self.illumos, 'add', '.')
        self._git(self.illumos, 'commit', '-qm', 'native baseline')
        upstream = lines.copy()
        upstream[15] = 'upstream middle\n'
        commit = self._commit('upstream change', {
            'module/zfs/main.c': ''.join(upstream),
        })
        preimage = self._git(self.openzfs, 'rev-parse',
                             f'{commit}^:module/zfs/main.c')
        output = self.root / 'three-way.mbox'
        result = self._run('--output', str(output), commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        probe = subprocess.run(
            ['git', '-C', str(self.illumos), 'cat-file', '-e', preimage],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(probe.returncode, 0)
        applied = self._import(commit)
        self.assertEqual(applied.returncode, 0, applied.stderr)
        self._git(self.illumos, 'cat-file', '-e', preimage)
        self.assertIn('3-way', applied.stdout)
        self.assertEqual(target.read_text().splitlines()[15], 'upstream middle')
        self.assertEqual(target.read_text().splitlines()[12], 'native middle')
        self.assertEqual(self._git(self.openzfs, 'rev-parse', 'HEAD'), commit)
        self.assertEqual(self._git(self.openzfs, 'status', '--porcelain'), '')

    def test_installed_layout_finds_modules_and_shipped_policy(self):
        prefix = self.root / 'installed/onbld'
        binary = prefix / 'bin/ozfs-import'
        binary.parent.mkdir(parents=True)
        binary.write_text(SCRIPT.read_text())
        library = prefix / ('lib/python%d.%d/onbld' % sys.version_info[:2])
        library.mkdir(parents=True)
        (library / '__init__.py').write_text('')
        shutil.copytree(SCRIPT.parent.parent / 'onbld/Ozfs',
                        library / 'Ozfs', ignore=shutil.ignore_patterns(
                            '__pycache__', 'Makefile*', 'py3'))
        (prefix / 'etc/ozfs').mkdir(parents=True)
        shutil.copyfile(self.policy, prefix / 'etc/ozfs/import-paths.conf')
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'main changed\n',
        })
        result = subprocess.run(
            [sys.executable, str(binary), '--source', str(self.openzfs),
             '--destination', str(self.illumos), '--check', commit],
            cwd=self.root, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)



    def test_header_rewriting_preserves_removed_header_like_content(self):
        original = '-- a/module/zfs/main.c\n++ b/module/zfs/main.c\n'
        self._commit('source baseline', {'module/zfs/main.c': original})
        target = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        target.write_text(original)
        self._git(self.illumos, 'add', '.')
        self._git(self.illumos, 'commit', '-qm', 'native baseline')
        commit = self._commit('remove header-like content', {
            'module/zfs/main.c': 'replacement\n',
        })
        result = self._import(commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(target.read_text(), 'replacement\n')

    def test_paths_with_spaces_quotes_and_unicode(self):
        for name in ['with space.c', 'quote"slash\\.c', 'caf\u00e9.c', 'Case%Name.c',
                     'literal[1].c', 'literal*.c']:
            source = 'module/zfs/' + name
            target = 'usr/src/uts/common/fs/zfs/' + name
            self._approve_addition(source, target)
            commit = self._commit('add unusual filename', {source: 'new\n'})
            result = self._import(commit)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((self.illumos / target).read_text(), 'new\n')



    def test_preparing_review_artifacts_does_not_copy_preimages(self):
        commit = self._commit('exceptional source change', {
            'module/zfs/exception.c': 'changed exception\n',
        })
        preimage = self._git(self.openzfs, 'rev-parse',
                             f'{commit}^:module/zfs/exception.c')
        output = self.root / 'exceptional.mbox'
        result = self._run('--output', str(output), commit)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('review required', result.stderr)
        self.assertTrue(output.is_file())
        probe = subprocess.run(
            ['git', '-C', str(self.illumos), 'cat-file', '-e', preimage],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(probe.returncode, 0)



    def test_apply_uses_message_hashes_and_preserves_edited_patch(self):
        commit = self._commit('source subject', {
            'module/zfs/main.c': 'upstream version\n',
        })
        patch = self.root / 'edited.patch'
        prepared = self._run('--output', str(patch), commit)
        self.assertEqual(prepared.returncode, 0, prepared.stderr)
        text = patch.read_text().replace('Subject: [PATCH] source subject',
                                         'Subject: [PATCH] reviewed subject')
        text = text.replace('+upstream version\n', '+reviewed version\n')
        # Only the commit-message entry supplies provenance, not the mbox From.
        text = text.replace(f'From {commit} Mon', 'From ' + '0' * 40 + ' Mon')
        patch.write_text(text)
        patch_bytes = patch.read_bytes()
        # Applying does not need the path policy or the preparation manifest.
        self.policy.unlink()
        Path(str(patch) + '.manifest.tsv').unlink()
        applied = self._run('--apply', str(patch))
        self.assertEqual(applied.returncode, 0, applied.stderr)
        target = self.illumos / 'usr/src/uts/common/fs/zfs/main.c'
        self.assertEqual(target.read_text(), 'reviewed version\n')
        self.assertEqual(self._git(self.illumos, 'show', '-s', '--format=%s'),
                         'reviewed subject')
        self.assertIn(f'OpenZFS {commit}', self._git(
            self.illumos, 'show', '-s', '--format=%B'))
        self.assertEqual(patch.read_bytes(), patch_bytes)

    def test_apply_rejects_hashes_present_only_in_diff(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'changed\n',
        })
        patch = self.root / 'missing-hashes.patch'
        self.assertEqual(self._run('--output', str(patch), commit).returncode, 0)
        text = patch.read_text().replace(f'OpenZFS {commit}\n', '')
        # A provenance-looking line in diff metadata must not supply a commit.
        text += f'OpenZFS {commit}\n'
        patch.write_text(text)
        before = self._git(self.illumos, 'rev-parse', 'HEAD')
        applied = self._run('--apply', str(patch))
        self.assertNotEqual(applied.returncode, 0)
        self.assertIn('no full OpenZFS hashes', applied.stderr)
        self.assertEqual(self._git(self.illumos, 'rev-parse', 'HEAD'), before)
        self.assertFalse((self.illumos / '.git/ozfs-import.manifest.tsv').exists())

    def test_apply_rejects_unknown_commit_message_hash(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'changed\n',
        })
        patch = self.root / 'unknown.patch'
        self.assertEqual(self._run('--output', str(patch), commit).returncode, 0)
        patch.write_text(patch.read_text().replace(
            f'OpenZFS {commit}', 'OpenZFS ' + 'f' * 40))
        applied = self._run('--apply', str(patch))
        self.assertNotEqual(applied.returncode, 0)
        self.assertIn('OpenZFS commit is not a commit', applied.stderr)
        self.assertFalse((self.illumos / '.git/rebase-apply').exists())

    def test_apply_rejects_multiple_mail_messages(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'changed\n',
        })
        patch = self.root / 'series.patch'
        self.assertEqual(self._run('--output', str(patch), commit).returncode, 0)
        patch.write_bytes(patch.read_bytes() * 2)
        applied = self._run('--apply', str(patch))
        self.assertNotEqual(applied.returncode, 0)
        self.assertIn('exactly one mail patch', applied.stderr)
        self.assertFalse((self.illumos / '.git/rebase-apply').exists())

    def test_apply_rejects_unprovided_preimage_before_copying(self):
        self._commit('source baseline', {
            'module/zfs/main.c': 'source baseline\n',
        })
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'changed\n',
        })
        oid = self._git(self.openzfs, 'rev-parse',
                        f'{commit}^:module/zfs/main.c')
        patch = self.root / 'wrong-preimage.patch'
        self.assertEqual(self._run('--output', str(patch), commit).returncode, 0)
        patch.write_text(patch.read_text().replace('index ' + oid,
                                                   'index ' + 'f' * 40))
        applied = self._run('--apply', str(patch))
        self.assertNotEqual(applied.returncode, 0)
        self.assertIn('not uniquely provided', applied.stderr)
        probe = subprocess.run(
            ['git', '-C', str(self.illumos), 'cat-file', '-e', oid],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(probe.returncode, 0)
        self.assertFalse((self.illumos / '.git/rebase-apply').exists())



    def test_policy_rejects_unknown_section(self):
        with self.policy.open('a') as stream:
            stream.write('\n[maap]\nmodule/zfs = usr/src/uts/common/fs/zfs\n')
        commit = self._commit('ordinary change', {'module/zfs/main.c': 'new\n'})
        result = self._run('--check', commit)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('unknown import path sections: maap', result.stderr)

    def test_policy_rejects_malformed_entries(self):
        commit = self._commit('ordinary change', {'module/zfs/main.c': 'new\n'})
        cases = [
            ('[map]\nmodule/zfs\n', 'missing destination'),
            ('[map]\nmodule/zfs = /absolute/path\n', 'invalid repository path'),
            ('[map]\nmodule/zfs = target\nmodule/zfs = other\n',
             'cannot read import path policy'),
            ('[exclude-openzfs]\nconfig = value\n', 'path must have no value'),
            ('[DEFAULT]\nmodule/zfs = target\n[map]\n',
             'DEFAULT values are not allowed'),
            ('[alternate-linux]\nmodule/os/linux/zfs/foo.c = usr/src/foo.c\n',
             'alternate has no primary mapping'),
        ]
        for contents, error in cases:
            with self.subTest(contents=contents):
                self.policy.write_text(contents)
                result = self._run('--check', commit)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(error, result.stderr)

    def test_explicit_header_mapping_selects_native_sys_directory(self):
        target = self.illumos / 'usr/src/uts/common/sys/native.h'
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('native\n')
        self._git(self.illumos, 'add', '.')
        self._git(self.illumos, 'commit', '-qm', 'native header')
        self._commit('source header baseline', {'include/sys/native.h': 'native\n'})
        self._set_policy('map', 'include/sys/native.h',
                         'usr/src/uts/common/sys/native.h')
        commit = self._commit('update native header', {
            'include/sys/native.h': 'native changed\n',
            'include/sys/kernel.h': 'zfs changed\n',
        })
        result = self._run('--check', commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('mapped\tinclude/sys/native.h\t'
                      'usr/src/uts/common/sys/native.h', result.stdout)
        self.assertIn('mapped\tinclude/sys/kernel.h\t'
                      'usr/src/uts/common/fs/zfs/sys/kernel.h', result.stdout)



    def test_patch_index_includes_review_paths_and_reasons(self):
        commit = self._commit('mixed changes', {
            'module/zfs/main.c': 'changed main\n',
            'module/zfs/new.c': 'new file\n',
            'README.md': 'review documentation\n',
        })
        patch = self.root / 'indexed.patch'
        result = self._run('--output', str(patch), commit)
        self.assertNotEqual(result.returncode, 0)
        text = patch.read_text()
        commentary = text.split('\n---\n', 1)[1].split('diff --git ', 1)[0]
        self.assertIn('3 files changed', commentary)
        self.assertIn('Requires review:\n', commentary)
        self.assertIn('module/zfs/new.c -> usr/src/uts/common/fs/zfs/new.c',
                      commentary)
        self.assertIn('new illumos path', commentary)
        self.assertIn('README.md', commentary)
        self.assertIn('unclassified path', commentary)
        self.assertLess(commentary.index('files changed'),
                        commentary.index('Requires review:'))
        self.assertNotIn('Requires review:', text.split('\n---\n', 1)[0])
        self.assertIn('+new file', text)

    def test_patch_index_is_not_part_of_imported_commit_message(self):
        commit = self._commit('reviewed new source', {
            'module/zfs/new.c': 'new file\n',
        })
        patch = self.root / 'reviewed-index.patch'
        prepared = self._run('--output', str(patch), commit)
        self.assertNotEqual(prepared.returncode, 0)
        self.assertIn('Requires review:', patch.read_text())
        applied = self._run('--apply', str(patch))
        self.assertEqual(applied.returncode, 0, applied.stderr)
        message = self._git(self.illumos, 'show', '-s', '--format=%B')
        self.assertIn(f'OpenZFS {commit}', message)
        self.assertNotIn('Requires review:', message)
        self.assertNotIn('file changed', message)
        self.assertEqual((self.illumos /
                          'usr/src/uts/common/fs/zfs/new.c').read_text(),
                         'new file\n')

    def test_clean_patch_has_index_without_review_heading(self):
        commit = self._commit('ordinary change', {
            'module/zfs/main.c': 'changed\n',
        })
        patch = self.root / 'indexed.patch'
        result = self._run('--output', str(patch), commit)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = patch.read_text()
        commentary = text.split('\n---\n', 1)[1].split('diff --git ', 1)[0]
        self.assertIn('1 file changed', commentary)
        self.assertIn('main.c', commentary)
        self.assertNotIn('Requires review:', commentary)


if __name__ == '__main__':
    unittest.main()
