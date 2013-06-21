#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
# Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
#

LIBRARY =	libfksmbsrv.a
VERS =		.1

OBJS_LOCAL = \
		fksmb_cred.o \
		fksmb_fem.o \
		fksmb_init.o \
		fksmb_kdoor.o \
		fake_lookup.o \
		fake_nblk.o \
		fake_vfs.o \
		fake_vnode.o \
		fake_vop.o \
		reparse.o \
		vncache.o

# NB: Intentionally ommitted:
#   smb_init, smb_fem, smb_kdoor, smb_vops
#
OBJS_FS_SMBSRV = \
		smb_acl.o				\
		smb_alloc.o				\
		smb_authenticate.o			\
		smb_close.o				\
		smb_common_open.o			\
		smb_common_transact.o			\
		smb_create.o				\
		smb_delete.o				\
		smb_directory.o				\
		smb_dispatch.o				\
		smb_echo.o				\
		smb_find.o				\
		smb_flush.o				\
		smb_fsinfo.o				\
		smb_fsops.o				\
		smb_kshare.o				\
		smb_kutil.o				\
		smb_lock.o				\
		smb_lock_byte_range.o			\
		smb_locking_andx.o			\
		smb_logoff_andx.o			\
		smb_mangle_name.o			\
		smb_mbuf_marshaling.o			\
		smb_mbuf_util.o				\
		smb_negotiate.o				\
		smb_net.o				\
		smb_node.o				\
		smb_nt_cancel.o				\
		smb_nt_create_andx.o			\
		smb_nt_transact_create.o		\
		smb_nt_transact_ioctl.o			\
		smb_nt_transact_notify_change.o		\
		smb_nt_transact_quota.o			\
		smb_nt_transact_security.o		\
		smb_odir.o				\
		smb_ofile.o				\
		smb_open_andx.o				\
		smb_opipe.o				\
		smb_oplock.o				\
		smb_pathname.o				\
		smb_print.o				\
		smb_process_exit.o			\
		smb_query_fileinfo.o			\
		smb_read.o				\
		smb_rename.o				\
		smb_sd.o				\
		smb_seek.o				\
		smb_server.o				\
		smb_session.o				\
		smb_session_setup_andx.o		\
		smb_set_fileinfo.o			\
		smb_signing.o				\
		smb_thread.o				\
		smb_tree.o				\
		smb_trans2_create_directory.o		\
		smb_trans2_dfs.o			\
		smb_trans2_find.o			\
		smb_tree_connect.o			\
		smb_unlock_byte_range.o			\
		smb_user.o				\
		smb_vfs.o				\
		smb_vops.o				\
		smb_vss.o				\
		smb_write.o				\
		smb_write_raw.o

# Most files from $SRC/common/smbsrv are built in libsmb
OBJS_CMN_SMBSRV = \
		smb_match.o \
		smb_netbios_util.o

OBJS_CMN_ACL = \
		acl_common.o

OBJS_MISC = \
		pathname.o \
		refstr.o

OBJECTS = \
	$(OBJS_LOCAL) \
	$(OBJS_FS_SMBSRV) \
	$(OBJS_CMN_SMBSRV) \
	$(OBJS_CMN_ACL) \
	$(OBJS_MISC)

include ../../../Makefile.lib
include ../../Makefile.lib

# Force SOURCEDEBUG
CSOURCEDEBUGFLAGS	= -g
CCSOURCEDEBUGFLAGS	= -g
CTFCVTFLAGS 	+= -g
CTFMRGFLAGS 	+= -g
STRIP_STABS 	= :


# hack hack - need our sys first
DTS_ERRNO += -I../../../libfakekernel/common
DTS_ERRNO += -I../common

INCS += -I$(SRC)/uts/common
INCS += -I$(SRC)/common/smbsrv
INCS += -I$(SRC)/common

LINTCHECKFLAGS += -erroff=E_INCONS_ARG_DECL2
LINTCHECKFLAGS += -erroff=E_INCONS_VAL_TYPE_DECL2
LINTCHECKFLAGS += -erroff=E_INCONS_VAL_TYPE_USED2

LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lfakekernel -lsmb -lcmdutils
LDLIBS +=	-lavl -lnvpair -lnsl -lmd -lreparse -lc

CPPFLAGS += $(INCS) -D_REENTRANT -D_FAKE_KERNEL
CPPFLAGS += -D_FILE_OFFSET_BITS=64
# Always want DEBUG here
CPPFLAGS += -DDEBUG

CERRWARN += -_gcc=-Wno-parentheses
CERRWARN += -_gcc=-Wno-switch

SRCS=   $(OBJS_LOCAL:%.o=$(SRCDIR)/%.c) \
	$(OBJS_FS_SMBSRV:%.o=$(SRC)/uts/common/fs/smbsrv/%.c) \
	$(OBJS_CMN_SMBSRV:%.o=$(SRC)/common/smbsrv/%.c) \
	$(OBJS_CMN_ACL:%.o=$(SRC)/common/acl/%.c)

all:

pics/%.o:	$(SRC)/uts/common/fs/smbsrv/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

pics/acl_common.o:	   $(SRC)/common/acl/acl_common.c
	$(COMPILE.c) -o $@ $(SRC)/common/acl/acl_common.c
	$(POST_PROCESS_O)

pics/pathname.o:	   $(SRC)/uts/common/fs/pathname.c
	$(COMPILE.c) -o $@ $(SRC)/uts/common/fs/pathname.c
	$(POST_PROCESS_O)

pics/refstr.o:		   $(SRC)/uts/common/os/refstr.c
	$(COMPILE.c) -o $@ $(SRC)/uts/common/os/refstr.c
	$(POST_PROCESS_O)

# Makefile.targ has rule for $(SRC)/common/smbsrv/%.c

.KEEP_STATE:

include ../../Makefile.targ
include ../../../Makefile.targ
