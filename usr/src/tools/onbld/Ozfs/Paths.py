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
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
import subprocess


PAIRED_POLICIES = ('addition',)
SIDES = ('illumos', 'openzfs')
MAPPED_GROUPS = ('map',)
GLOB_PATHSPEC_PREFIX = ':(glob)'
ALLOWED_SETTINGS = {
    'repositories': {'openzfs', 'illumos'},
    'paths': {'import-paths'},
}


class OzfsError(Exception):
    pass


@dataclass(frozen=True)
class PlatformMapping:
    role: str
    variant: str
    illumos_path: str
    openzfs_path: str


@dataclass(frozen=True)
class PathAction:
    kind: str
    source: str
    target: str = ''
    group: str = ''
    reason: str = ''


def load_config(path, defaults, source=None, destination=None, policy=None):
    config = configparser.ConfigParser(interpolation=None, strict=True)
    base = Path.cwd()
    if path is not None:
        config_path = Path(path).expanduser().resolve()
        base = config_path.parent
        try:
            with config_path.open(encoding='utf-8') as stream:
                config.read_file(stream)
        except (OSError, configparser.Error) as error:
            raise OzfsError(
                f'cannot read configuration file {config_path}: {error}')

    for section in config.sections():
        if section not in ALLOWED_SETTINGS:
            raise OzfsError(f'unknown configuration section: {section}')
        for setting in config[section]:
            if setting not in ALLOWED_SETTINGS[section]:
                raise OzfsError(
                    f'unknown configuration setting: {section}.{setting}')

    def setting(section, key):
        value = config.get(section, key, fallback='').strip()
        return resolve_config_path(value, base) if value else None

    openzfs_repo = source or setting('repositories', 'openzfs')
    if openzfs_repo is None:
        raise OzfsError('specify --source or repositories.openzfs in --config')
    illumos_repo = (destination or setting('repositories', 'illumos') or
                    Path.cwd())
    policy_path = policy or setting('paths', 'import-paths') or defaults
    return tuple(Path(value).expanduser().resolve() for value in
                 (illumos_repo, openzfs_repo, policy_path))


def resolve_config_path(value, base):
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = base / path
    return path.resolve()


def load_path_policy(path):
    config = configparser.ConfigParser(
        interpolation=None, strict=True, allow_no_value=True,
        delimiters=('=',), empty_lines_in_values=False)
    config.optionxform = str
    try:
        with path.open(encoding='utf-8') as stream:
            config.read_file(stream)
    except (OSError, configparser.Error) as error:
        raise OzfsError(f'cannot read import path policy {path}: {error}')
    if config.defaults():
        raise OzfsError('DEFAULT values are not allowed in import path policy')

    mapping_sections = {'map': 'map', 'additions': 'addition'}
    list_sections = {
        f'{kind}-{side}': (group, side)
        for kind, group in [('exclude', 'exclude'), ('review', 'exception')]
        for side in SIDES
    }
    platform_sections = {
        f'{role}-{variant}': (role, variant)
        for role in ('primary', 'alternate')
        for variant in ('freebsd', 'linux')
    }
    allowed = set(mapping_sections) | set(list_sections) | set(platform_sections)
    unknown = set(config.sections()) - allowed
    if unknown:
        raise OzfsError('unknown import path sections: ' + ', '.join(sorted(unknown)))
    result = {
        group: {side: [] for side in SIDES}
        for group in ('map', 'addition', 'exclude', 'exception')
    }
    mappings = []
    sources = set()
    primary_targets = set()
    for section in config.sections():
        for source, value in config.items(section):
            location = f'{path} [{section}]'
            if section in list_sections:
                if value is not None:
                    raise OzfsError(f'path must have no value in {location}: {source}')
                if source.startswith(GLOB_PATHSPEC_PREFIX):
                    validate_glob_pathspec(source, location, source)
                else:
                    validate_repo_path(source, location, source)
                group, side = list_sections[section]
                result[group][side].append(source)
                continue
            validate_repo_path(source, location, source)
            if not value or not value.strip():
                raise OzfsError(f'missing destination in {location}: {source}')
            targets = [target.strip() for target in value.splitlines()]
            for target in targets:
                validate_repo_path(target, location, source)
            if section in mapping_sections:
                group = mapping_sections[section]
                for target in targets:
                    result[group]['openzfs'].append(source)
                    result[group]['illumos'].append(target)
                continue
            role, variant = platform_sections[section]
            if len(targets) != 1:
                raise OzfsError(f'platform mapping needs one destination: {source}')
            target = targets[0]
            if source in sources:
                raise OzfsError(f'duplicate platform OpenZFS path: {source}')
            sources.add(source)
            if role == 'primary':
                if target in primary_targets:
                    raise OzfsError(f'duplicate primary illumos path: {target}')
                primary_targets.add(target)
            mappings.append(PlatformMapping(role, variant, target, source))

    for mapping in mappings:
        if (mapping.role == 'alternate' and
                mapping.illumos_path not in primary_targets):
            raise OzfsError('alternate has no primary mapping: ' +
                            mapping.illumos_path)
    return result, mappings


def validate_repo_path(value, name, line_number):
    path = PurePosixPath(value)
    if (not value or value == '.' or path.is_absolute() or
            '..' in path.parts or value.startswith(':')):
        raise OzfsError(
            f'invalid repository path in {name}:{line_number}: {value}')


def validate_glob_pathspec(value, name, line_number):
    pattern = value[len(GLOB_PATHSPEC_PREFIX):]
    path = PurePosixPath(pattern)
    if (not pattern or path.is_absolute() or '..' in path.parts or
            pattern.startswith(':')):
        raise OzfsError(
            f'invalid glob pathspec in {name}:{line_number}: {value}')


def validate_repository(path, label):
    if not path.is_dir():
        raise OzfsError(f'{label} repository is not a directory: {path}')

    result = run_git(path, 'rev-parse', '--show-toplevel')
    if result.returncode != 0:
        raise OzfsError(
            f'{label} repository is not a Git work tree: {path}')

    return Path(result.stdout.decode().strip()).resolve()


def resolve_commit(repo, value, label):
    result = run_git(
        repo, 'rev-parse', '--verify', '--end-of-options',
        f'{value}^{{commit}}')
    if result.returncode != 0:
        raise OzfsError(f'{label} commit is not a commit: {value}')
    return result.stdout.decode().strip()


def path_is_under(path, root):
    return path == root or path.startswith(f'{root}/')


def map_relative_path(path, source_root, target_root):
    if path == source_root:
        return target_root
    return f'{target_root}/{path[len(source_root) + 1:]}'


def git_output(repo, *args, input_data=None):
    result = run_git(repo, *args, input_data=input_data)
    if result.returncode != 0:
        message = result.stderr.decode(errors='replace').strip()
        raise OzfsError(f'Git command failed: {message}')
    return result.stdout


def changed_paths(repo, before, after):
    output = git_output(
        repo, 'diff', '--name-status', '-z', '--no-renames',
        before, after, '--')
    fields = output.split(b'\0')
    if fields and not fields[-1]:
        fields.pop()
    if len(fields) % 2 != 0:
        raise OzfsError(
            f'unexpected path list for OpenZFS range {before}..{after}')

    paths = []
    for index in range(0, len(fields), 2):
        status = fields[index].decode(errors='replace')
        path = fields[index + 1].decode()
        if status not in ('A', 'D', 'M', 'T'):
            raise OzfsError(
                f'unsupported change status {status} for {path}')
        paths.append(path)
    return paths


def path_matches_any(path, roots):
    candidate = PurePosixPath(path)
    return any(
        candidate.match(root[len(GLOB_PATHSPEC_PREFIX):])
        if root.startswith(GLOB_PATHSPEC_PREFIX)
        else path_is_under(path, root)
        for root in roots
    )


def mapped_candidates(path, path_lists, groups=MAPPED_GROUPS):
    candidates = []
    for group in groups:
        pairs = zip(
            path_lists[group]['openzfs'],
            path_lists[group]['illumos'])
        for source_root, target_root in pairs:
            if path_is_under(path, source_root):
                candidates.append((
                    group,
                    source_root,
                    map_relative_path(path, source_root, target_root),
                ))
    if not candidates:
        return []

    longest = max(
        len(source.split('/')) for _group, source, _target in candidates)
    return [
        candidate for candidate in candidates
        if len(candidate[1].split('/')) == longest
    ]


def tree_has_path(repo, commit, path):
    result = run_git(repo, 'cat-file', '-e', f'{commit}:{path}')
    return result.returncode == 0


def resolve_mapped_path(
        illumos_repo, illumos_commit, path, path_lists,
        groups=MAPPED_GROUPS):
    candidates = mapped_candidates(path, path_lists, groups)
    targets = sorted({target for _group, _source, target in candidates})
    if not targets:
        return None
    if len(targets) == 1:
        group = next(
            group for group, _source, target in candidates
            if target == targets[0])
        return group, targets[0]

    existing = [
        target for target in targets
        if tree_has_path(illumos_repo, illumos_commit, target)
    ]
    if len(existing) == 1:
        group = next(
            group for group, _source, target in candidates
            if target == existing[0])
        return group, existing[0]

    choices = ', '.join(targets)
    raise OzfsError(f'ambiguous mapping for {path}: {choices}')


def resolve_platform_action(path, platform_mappings):
    candidates = [
        mapping for mapping in platform_mappings
        if path_is_under(path, mapping.openzfs_path)
    ]
    if not candidates:
        return None

    longest = max(
        len(mapping.openzfs_path.split('/')) for mapping in candidates)
    candidates = [
        mapping for mapping in candidates
        if len(mapping.openzfs_path.split('/')) == longest
    ]
    if len(candidates) != 1:
        raise OzfsError(f'ambiguous platform mapping for {path}')

    mapping = candidates[0]
    if mapping.role == 'alternate':
        return PathAction('alternate', path, group='core')
    target = map_relative_path(
        path, mapping.openzfs_path, mapping.illumos_path)
    return PathAction('mapped', path, target, 'core')


def check_mapped_target(
        action, path_lists, reject_exception, preserve_review):
    if (action.kind == 'mapped' and
            path_matches_any(
                action.target, path_lists['exclude']['illumos'])):
        raise OzfsError(
            f'mapped illumos path is excluded: '
            f'{action.source} -> {action.target}')
    if (reject_exception and action.kind == 'mapped' and
            path_matches_any(
                action.target, path_lists['exception']['illumos'])):
        if preserve_review:
            return PathAction(
                'review', action.source, action.target, action.group,
                'exceptional illumos target')
        raise OzfsError(
            f'mapped illumos path is exceptional: '
            f'{action.source} -> {action.target}')
    return action


def classify_path(
        illumos_repo, illumos_commit, path, path_lists, platform_mappings,
        reject_exception=True, preserve_review=False):
    if (reject_exception and
            path_matches_any(path, path_lists['exception']['openzfs'])):
        if preserve_review:
            return PathAction(
                'review', path, path, reason='exceptional OpenZFS path')
        raise OzfsError(f'exceptional path requires review: {path}')

    if path_matches_any(path, path_lists['exclude']['openzfs']):
        return PathAction('excluded', path)

    try:
        addition = resolve_mapped_path(
            illumos_repo, illumos_commit, path, path_lists, PAIRED_POLICIES)
    except OzfsError as error:
        if preserve_review:
            return PathAction('review', path, path, reason=str(error))
        raise
    if addition is not None:
        group, target = addition
        action = PathAction('mapped', path, target, group)
        return check_mapped_target(
            action, path_lists, reject_exception, preserve_review)

    try:
        platform_action = resolve_platform_action(path, platform_mappings)
    except OzfsError as error:
        if preserve_review:
            return PathAction('review', path, path, reason=str(error))
        raise
    if platform_action is not None:
        return check_mapped_target(
            platform_action, path_lists, reject_exception, preserve_review)

    try:
        mapped = resolve_mapped_path(
            illumos_repo, illumos_commit, path, path_lists)
    except OzfsError as error:
        if preserve_review:
            return PathAction('review', path, path, reason=str(error))
        raise
    if mapped is not None:
        group, target = mapped
        action = PathAction('mapped', path, target, group)
        return check_mapped_target(
            action, path_lists, reject_exception, preserve_review)

    if preserve_review:
        return PathAction('review', path, path, reason='unclassified path')
    raise OzfsError(f'unclassified path: {path}')


def run_git(repo, *args, input_data=None):
    return subprocess.run(
        ['git', '-C', str(repo), *args],
        input=input_data, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        check=False)
