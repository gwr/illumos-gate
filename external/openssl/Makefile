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
# Copyright 2015 <contributor>
#


COMPONENT_NAME =	openssl

# When updating to a new version of OpenSSL, you must update both
# COMPONENT_VERSION here, and both that and IPS_COMPONENT_VERSION
# found in $SRC/Makefile.external
COMPONENT_VERSION =	1.0.1h

COMPONENT_PROJECT_URL=	http://www.openssl.org/
COMPONENT_SRC =		$(COMPONENT_NAME)-$(COMPONENT_VERSION)
COMPONENT_ARCHIVE =	$(COMPONENT_SRC).tar.gz

# Apply the patches, and then two more changes:
# 1: Symlink md2.h into include/openssl because
#    that's where our install_h looks for headers.
# 2: Rename the provided opensslconf.h to make sure
#    we don't accidently use that instead of our
#    customized one that supports both 32/64-bit.
#    (See $SRC/lib/openssl/libcrypto/common)
all patch: patch.stamp
patch.stamp: unpack.stamp
	for p in patches/*.patch ; do ;\
	  echo "patch $$p" ;\
	  (cd $(COMPONENT_SRC) && patch -p1) < $$p ;\
	done
	(cd $(COMPONENT_SRC)/include/openssl && \
	 ln -s ../../crypto/md2/md2.h . )
	(cd $(COMPONENT_SRC)/crypto && \
	 mv -f opensslconf.h opensslconf.h.bak )
	touch $@

unpack: unpack.stamp
unpack.stamp: $(COMPONENT_ARCHIVE)
	/usr/bin/rm -rf $(COMPONENT_SRC)
	/usr/bin/tar xvfj $(COMPONENT_ARCHIVE)
	touch $@

# Could either have a download rule for $(COMPONENT_ARCHIVE)
# or just check it in.  Let's just check it in.
# $(COMPONENT_ARCHIVE) :
#	wget ...

clean clobber:
	/usr/bin/rm -rf $(COMPONENT_SRC)
	/usr/bin/rm -rf patch.stamp unpack.stamp
