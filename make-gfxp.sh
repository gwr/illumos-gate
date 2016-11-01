#!/bin/sh

[ "$SRC" ] || { echo "Must set SRC=... first!" ; exit 1; }

(cd usr/src/uts/common/sys && make install_h)
(cd usr/src/uts/i86pc/gfx_private && make install)

gfxp_files="usr/include/devfsadm.h
usr/include/sys/gfx_private.h
platform/i86pc/kernel/misc/amd64/gfx_private
platform/i86pc/kernel/misc/gfx_private"

(cd proto/root_i386 && tar cf ../../gfxp.tar $gfxp_files)
