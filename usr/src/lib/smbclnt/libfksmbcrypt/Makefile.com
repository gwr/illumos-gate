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
# Copyright 2018 Nexenta Systems, Inc.  All rights reserved.
#
# Copyright 2019, Joyent, Inc.
#

LIBRARY =	libfksmbcrypt.a
VERS =		.1

OBJS_LOCAL = \
	fksmb_crypt_pkcs.o \
	fksmb_sign_pkcs.o

# See also: OBJS_SMBCRYPT in $SRC/uts/common/Makefile.files
OBJS_SMBCRYPT = smb_kdf.o

OBJECTS = \
	$(OBJS_LOCAL) \
	$(OBJS_SMBCRYPT)

include ../../../Makefile.lib
include ../../Makefile.lib

# Force SOURCEDEBUG
CSOURCEDEBUGFLAGS	= -g
CCSOURCEDEBUGFLAGS	= -g
STRIP_STABS	= :

# Note: need our sys includes _before_ ENVCPPFLAGS, proto etc.
# Also, like Makefile.uts, reset CPPFLAGS
CPPFLAGS.first += -I../../../libfakekernel/common
CPPFLAGS.first += -I../common
CPPFLAGS = $(CPPFLAGS.first)

INCS += -I$(SRC)/uts/common

CPPFLAGS += $(INCS) -D_REENTRANT -D_FAKE_KERNEL
CPPFLAGS += -D_FILE_OFFSET_BITS=64
# Always want DEBUG here
CPPFLAGS += -DDEBUG

CERRWARN += -_gcc=-Wno-switch

# needs work
SMOFF += all_func_returns,deref_check,signed

# Have LDLIBS32, LDLIBS64 from ../Makefile.lib
LDLIBS +=	$(MACH_LDLIBS)
LDLIBS +=	-lfakekernel -lpkcs11 -lc

SMBCRYPT_DIR=$(SRC)/uts/common/fs/smbcrypt

all:

pics/%.o:	$(SMBCRYPT_DIR)/%.c
	$(COMPILE.c) -o $@ $<
	$(POST_PROCESS_O)

.KEEP_STATE:

include ../../Makefile.targ
include ../../../Makefile.targ
