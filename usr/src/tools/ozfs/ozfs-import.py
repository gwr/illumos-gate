#!@TOOLS_PYTHON@ -Es
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

import os
from pathlib import Path
import sys

sys.path.insert(1, os.path.join(os.path.dirname(__file__), '..', 'lib',
                              'python%d.%d' % sys.version_info[:2]))
sys.path.insert(2, os.path.join(os.path.dirname(__file__), '..'))

from onbld.Ozfs.Import import main

if __name__ == '__main__':
    tools = Path(__file__).resolve().parent.parent
    in_source = Path(__file__).resolve().parent.name == 'ozfs'
    defaults = tools / ('ozfs' if in_source else 'etc/ozfs')
    sys.exit(main(defaults / 'import-paths.conf'))
