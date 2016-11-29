#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the License at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

typeset TESTDIR="$TEST_BASE_DIR/zpool-status-c.$$"
typeset PATHDIR="$TESTDIR/path"
typeset PATHDIR2="$TESTDIR/path2"
typeset HOMEDIR="$TESTDIR/home"
typeset PATHSCRIPT="$PATHDIR/pathscript"
typeset PATHSCRIPT2="$PATHDIR2/pathscript2"
typeset HOMESCRIPT="$HOMEDIR/.zpool.d/homescript"

function cleanup
{
	rm -rf "$TESTDIR"
}

log_onexit cleanup
log_assert "zpool status -c honors its enable and search-path controls"

log_must mkdir -p "$PATHDIR" "$PATHDIR2" "$HOMEDIR/.zpool.d"
cat > "$PATHSCRIPT" <<EOF
#!/bin/sh
echo "PATHCOL=PATHVAL"
EOF
cat > "$PATHSCRIPT2" <<EOF
#!/bin/sh
echo "PATHCOL2=PATHVAL2"
EOF
cat > "$HOMESCRIPT" <<EOF
#!/bin/sh
echo "HOMECOL=HOMEVAL"
EOF
log_must chmod +x "$PATHSCRIPT" "$PATHSCRIPT2" "$HOMESCRIPT"

export ZPOOL_SCRIPTS_AS_ROOT=1
export ZPOOL_SCRIPTS_PATH="$PATHDIR:$PATHDIR2"
test_zpool_script pathscript "$TESTPOOL" "zpool status -P -c"
test_zpool_script pathscript2 "$TESTPOOL" "zpool status -P -c"
test_zpool_script pathscript,pathscript2 "$TESTPOOL" "zpool status -P -c"

typeset output
output=$(zpool status -P -c "$PATHSCRIPT" "$TESTPOOL")
if echo "$output" | grep -q PATHVAL; then
	log_fail "zpool status -c accepted a script containing '/'"
fi

unset ZPOOL_SCRIPTS_PATH
export HOME="$HOMEDIR"
test_zpool_script homescript "$TESTPOOL" "zpool status -P -c"

export ZPOOL_SCRIPTS_ENABLED=0
log_mustnot zpool status -c homescript "$TESTPOOL"
export ZPOOL_SCRIPTS_ENABLED=1
log_must zpool status -c homescript "$TESTPOOL"

log_pass "zpool status -c honors its enable and search-path controls"
