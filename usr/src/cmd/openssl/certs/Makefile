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

include ../../../Makefile.master

# XXX Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h

CERTS= \
	ca-cert.pem \
	dsa-ca.pem \
	dsa-pca.pem \
	pca-cert.pem

all: hash.stamp

hash.stamp: $(CERTS)
	for f in $(CERTS) ; do; \
	  h=`../util/shlib_wrap.sh ../apps/openssl x509 -hash -noout -in $$f`; \
	  rm -f $$h.0; ln -s $$f $$h.0; \
	done
	touch hash.stamp

%.pem : $(OPENSSLDIR)/certs/demo/%.pem
	-$(RM) $@
	cp $< $@

install: all

lint:

clean:

clobber:
	$(RM) $(CERTS) hash.stamp *.0
