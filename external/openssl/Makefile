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
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

COMPONENT_NAME =	openssl

# When updating to a new version of OpenSSL, you must update both
# COMPONENT_VERSION here, and OPENSSL_VERSION, OPENSSL_PKGVERS
# found in $SRC/Makefile.master (Yes, should improve on that.)
COMPONENT_VERSION =	1.0.1m

COMPONENT_PROJECT_URL=	http://www.openssl.org/
COMPONENT_SRC =		$(COMPONENT_NAME)-$(COMPONENT_VERSION)
COMPONENT_ARCHIVE =	$(COMPONENT_SRC).tar.gz
COMPONENT_ARCHIVE_HASH=	\
    sha1:4ccaf6e505529652f9fdafa01d1d8300bd9f3179

COMPONENT_ARCHIVE_URL =	$(COMPONENT_PROJECT_URL)source/$(COMPONENT_ARCHIVE)

# Apply the patches, and then a few more changes:
# 1: Symlink md2.h into include/openssl because
#    that's where our install_h looks for headers.
# 2: Replace the provided opensslconf.h with one
#    customized to support both 32/64-bit etc.
# 3: Add sunw_prefix.h (for private openssl)
all install patch: patch.stamp
patch.stamp: unpack.stamp
	cd $(COMPONENT_SRC) && \
	for p in ../patches/*.patch ; do ;\
	  patch -bs -p1 < $$p ;\
	done
	cd $(COMPONENT_SRC)/crypto && \
	  mv -f opensslconf.h opensslconf.h.orig
	cp files/opensslconf.h $(COMPONENT_SRC)/crypto
	cp files/sunw_prefix.h $(COMPONENT_SRC)/crypto
	cd $(COMPONENT_SRC)/include/openssl && \
	  ln -s ../../crypto/md2/md2.h .
	cd $(COMPONENT_SRC)/include/openssl && \
	  ln -s ../../crypto/sunw_prefix.h .
	touch $@

unpack: unpack.stamp
unpack.stamp: hash.stamp
	/usr/bin/rm -rf $(COMPONENT_SRC)
	/usr/bin/tar xfj $(COMPONENT_ARCHIVE)
	touch $@

# The user may put this archive in place by hand if downloading it
# would be inconvenient.  If it exists, we'll just check the hash.
hash.stamp: $(COMPONENT_ARCHIVE)
	h=`digest -a sha1 $(COMPONENT_ARCHIVE)` ;\
	[ sha1:$$h = $(COMPONENT_ARCHIVE_HASH) ]
	touch $@

# Download to a tmp file to avoid the possibility of leaving a
# partial download if it can't complete for any reason.
$(COMPONENT_ARCHIVE):
	/usr/bin/rm -f $(COMPONENT_SRC).tmp
	wget -q -O $(COMPONENT_SRC).tmp $(COMPONENT_ARCHIVE_URL)
	/usr/bin/mv $(COMPONENT_SRC).tmp $@

# Nothing to install here.
install:

# Just remove the unpacked and patched tree
clean clobber:
	/usr/bin/rm -f hash.stamp patch.stamp unpack.stamp
	/usr/bin/rm -rf $(COMPONENT_SRC)

# clobber does not remove downloaded files, but this will.
clobber_downloads:
	/usr/bin/rm -f $(COMPONENT_ARCHIVE)
