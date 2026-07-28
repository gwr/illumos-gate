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

"""
Verify that every symbols configuration has at least one entry that
names exactly the "ALL" compilation environments, so we know that
every header file _compiles_ in all compilation environments.
(even if it exposes nothing of interest)

Note this is a _build_ tool for "make check", not installed.
"""

import os
import sys

# Import from ../tests/common/symbol_test.py
COMMON = os.path.join(os.path.dirname(__file__), '..', 'tests', 'common')
sys.path.insert(0, COMMON)

from symbol_test import SymConfig


def main(paths):
    if not paths:
        sys.exit(f'usage: {sys.argv[0]} symbols.cfg ...')

    failed = False

    for path in paths:
        cfg = SymConfig()
        cfg.load(path)

        if not any(entry.env_spec == 'ALL' for entry in cfg.entries):
            print(f'{path}: no entry has environment specification ALL',
                  file=sys.stderr)
            failed = True

    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
