#
# This file is derived from the OpenSSL file:
#   test/Makefile
# which carried no copyright, but can be presumed to be covered by
# Copyright (c) 1998-2011 The OpenSSL Project.  All rights reserved.
# as shown in the OpenSSL LICENSE file.
#

# Test programs - from the OpenSSL Makefile
BNTEST=		bntest
ECTEST=		ectest
ECDSATEST=	ecdsatest
ECDHTEST=	ecdhtest
EXPTEST=	exptest
IDEATEST=	ideatest
SHATEST=	shatest
SHA1TEST=	sha1test
SHA256TEST=	sha256t
SHA512TEST=	sha512t
MDC2TEST=	mdc2test
RMDTEST=	rmdtest
MD2TEST=	md2test
MD4TEST=	md4test
MD5TEST=	md5test
HMACTEST=	hmactest
WPTEST=		wp_test
RC2TEST=	rc2test
RC4TEST=	rc4test
RC5TEST=	rc5test
BFTEST=		bftest
CASTTEST=	casttest
DESTEST=	destest
RANDTEST=	randtest
DHTEST=		dhtest
DSATEST=	dsatest
METHTEST=	methtest
SSLTEST=	ssltest
RSATEST=	rsa_test
ENGINETEST=	enginetest
EVPTEST=	evp_test
IGETEST=	igetest
JPAKETEST=	jpaketest
SRPTEST=	srptest
ASN1TEST=	asn1test
HEARTBEATTEST= heartbeat_test

EXE=	$(BNTEST) $(ECTEST)  $(ECDSATEST) $(ECDHTEST) $(IDEATEST) \
	$(MD2TEST)  $(MD4TEST) $(MD5TEST) $(HMACTEST) $(WPTEST) \
	$(RC2TEST) $(RC4TEST) $(RC5TEST) \
	$(DESTEST) $(SHATEST) $(SHA1TEST) $(SHA256TEST) $(SHA512TEST) \
	$(MDC2TEST) $(RMDTEST) \
	$(RANDTEST) $(DHTEST) $(ENGINETEST) \
	$(BFTEST) $(CASTTEST) $(SSLTEST) $(EXPTEST) $(DSATEST) $(RSATEST) \
	$(EVPTEST) $(IGETEST) $(JPAKETEST) $(SRPTEST) \
	$(ASN1TEST) $(HEARTBEATTEST)

# Test targets - from the OpenSSL Makefile

TESTS= \
	test_des test_idea test_sha test_md4 test_md5 test_hmac \
	test_md2 test_mdc2 test_wp \
	test_rmd test_rc2 test_rc4 test_rc5 test_bf test_cast test_aes \
	test_rand test_bn test_ec test_ecdsa test_ecdh \
	test_enc test_x509 test_rsa test_crl test_sid \
	test_gen test_req test_pkcs7 test_verify test_dh test_dsa \
	test_ss test_ca test_engine test_evp test_ssl test_tsa test_ige \
	test_jpake test_srp test_cms test_heartbeat

SCRIPTS= smcont.txt testenc tx509 trsa tcrl tsid treq tpkcs7 tpkcs7d \
	testgen test.cnf testss testfipsssl testssl testsslproxy testca \
	testtsa cms-test.pl CAss.cnf Uss.cnf P1ss.cnf P2ss.cnf CAtsa.cnf

PEMS=	testx509.pem testrsa.pem testcrl.pem testsid.pem testx509.pem \
	v3-cert1.pem v3-cert2.pem testreq2.pem testp7.pem pkcs7-1.pem

PROG=	$(EXE)

include ../../Makefile.cmd

# XXX Get this from some included makefile?
OPENSSLDIR = $(CODEMGR_WS)/external/openssl/openssl-1.0.1h
SRCDIR = $(OPENSSLDIR)

# For: e_os.h e_os2.h
CPPFLAGS +=	-I$(OPENSSLDIR)

# Our (PIC) libraries do OPENSSL_cpuid_setup in .init so
# don't try to call that explicitly. (it's also static)
CPPFLAGS +=	-DOPENSSL_PIC

LDLIBS +=	-lssl -lcrypto -lsocket -lnsl

CLOBBERFILES += .rnd p tmp.*test p.* newkey.pem certCA.srl certCA.ss \
	certP1.srl certP1.ss certP2.ss certU.srl certU.ss \
	intP1.ss intP2.ss keyCA.ss keyP1.ss keyP2.ss keyU.ss \
	req2CA.ss reqCA.ss reqP1.ss reqP2.ss reqU.ss \
	testkey.pem testreq.pem tmp_intP1.ss tmp_intP2.ss

alltests: all $(TESTS)

all: $(PROG) $(SCRIPTS) $(PEMS)

% : $(SRCDIR)/test/%.c
	$(LINK.c) -o $@ $< $(LDLIBS)
	$(POST_PROCESS)

% : $(SRCDIR)/test/%
	-$(RM) $@
	cp $< $@

# Nothing to install.  This builds and runs programs here,
# or runs things from the proto area (i.e. openssl cmd).
install: all

clean:
	-$(RM) $(PROG)

clobber: clean
	-$(RM) $(SCRIPTS) $(PEMS) $(CLOBBERFILES)
	-$(RM) -r smime-certs

test_evp: $(EVPTEST) FRC
	../util/shlib_wrap.sh ./$(EVPTEST) $(OPENSSLDIR)/test/evptests.txt

test_des: $(DESTEST) FRC
	../util/shlib_wrap.sh ./$(DESTEST)

test_idea: $(IDEATEST) FRC
	../util/shlib_wrap.sh ./$(IDEATEST)

test_sha: $(SHATEST) $(SHA1TEST) $(SHA256TEST) $(SHA512TEST) FRC
	../util/shlib_wrap.sh ./$(SHATEST)
	../util/shlib_wrap.sh ./$(SHA1TEST)
	../util/shlib_wrap.sh ./$(SHA256TEST)
	../util/shlib_wrap.sh ./$(SHA512TEST)

test_mdc2: $(MDC2TEST) FRC
	../util/shlib_wrap.sh ./$(MDC2TEST)

test_md5: $(MD5TEST) FRC
	../util/shlib_wrap.sh ./$(MD5TEST)

test_md4: $(MD4TEST) FRC
	../util/shlib_wrap.sh ./$(MD4TEST)

test_hmac: $(HMACTEST) FRC
	../util/shlib_wrap.sh ./$(HMACTEST)

test_wp: $(WPTEST) FRC
	../util/shlib_wrap.sh ./$(WPTEST)

test_md2: $(MD2TEST) FRC
	../util/shlib_wrap.sh ./$(MD2TEST)

test_rmd: $(RMDTEST) FRC
	../util/shlib_wrap.sh ./$(RMDTEST)

test_bf: $(BFTEST) FRC
	../util/shlib_wrap.sh ./$(BFTEST)

test_cast: $(CASTTEST) FRC
	../util/shlib_wrap.sh ./$(CASTTEST)

test_rc2: $(RC2TEST) FRC
	../util/shlib_wrap.sh ./$(RC2TEST)

test_rc4: $(RC4TEST) FRC
	../util/shlib_wrap.sh ./$(RC4TEST)

test_rc5: $(RC5TEST) FRC
	../util/shlib_wrap.sh ./$(RC5TEST)

test_rand: $(RANDTEST) FRC
	../util/shlib_wrap.sh ./$(RANDTEST)

test_enc: testenc FRC
	@sh -x ./testenc

test_x509: tx509 testx509.pem v3-cert1.pem v3-cert2.pem FRC
	@echo test normal x509v1 certificate
	sh ./tx509 2>/dev/null
	@echo test first x509v3 certificate
	sh ./tx509 v3-cert1.pem 2>/dev/null
	@echo test second x509v3 certificate
	sh ./tx509 v3-cert2.pem 2>/dev/null

test_rsa: $(RSATEST) trsa testrsa.pem FRC
	@sh ./trsa 2>/dev/null
	../util/shlib_wrap.sh ./$(RSATEST)

test_crl: tcrl testcrl.pem FRC
	@sh ./tcrl 2>/dev/null

test_sid: tsid testsid.pem FRC
	@sh ./tsid 2>/dev/null

test_req: treq testreq2.pem FRC
	@sh ./treq 2>/dev/null
	@sh ./treq testreq2.pem 2>/dev/null

test_pkcs7: tpkcs7 testp7.pem tpkcs7d pkcs7-1.pem FRC
	@sh ./tpkcs7 2>/dev/null
	@sh ./tpkcs7d 2>/dev/null

# Note this used to run: sh -x -c "`sh ./bctest ignore`"
# which should print just: "sed 's/print.*//' | bc"
# Substituted that so we don't have to run bctest.
test_bn: $(BNTEST) $(EXPTEST) FRC
	@echo starting big number library test, could take a while...
	@../util/shlib_wrap.sh ./$(BNTEST) >tmp.bntest
	@echo quit >>tmp.bntest
	@echo "running bc"
	@<tmp.bntest sed -e 's/print.*//' | bc | $(PERL) -e '$$i=0; while (<STDIN>) {if (/^test (.*)/) {print STDERR "\nverify $$1";} elsif (!/^0\r?$$/) {die "\nFailed! bc: $$_";} else {print STDERR "."; $$i++;}} print STDERR "\n$$i tests passed\n"'
	@echo 'test a^b%c implementations'
	../util/shlib_wrap.sh ./$(EXPTEST)

test_ec: $(ECTEST) FRC
	@echo 'test elliptic curves'
	../util/shlib_wrap.sh ./$(ECTEST)

test_ecdsa: $(ECDSATEST) FRC
	@echo 'test ecdsa'
	../util/shlib_wrap.sh ./$(ECDSATEST)

test_ecdh: $(ECDHTEST) FRC
	@echo 'test ecdh'
	../util/shlib_wrap.sh ./$(ECDHTEST)

test_verify: FRC
	@echo "The following command should have some OK's and some failures"
	@echo "There are definitly a few expired certificates"
	../util/shlib_wrap.sh ../apps/openssl verify -CApath \
	  ../certs ../certs/*.pem

test_dh: $(DHTEST) FRC
	@echo "Generate a set of DH parameters"
	../util/shlib_wrap.sh ./$(DHTEST)

test_dsa: $(DSATEST) FRC
	@echo "Generate a set of DSA parameters"
	../util/shlib_wrap.sh ./$(DSATEST)
	../util/shlib_wrap.sh ./$(DSATEST) -app2_1

test_gen: testgen test.cnf FRC
	@echo "Generate and verify a certificate request"
	@sh ./testgen

test_ss keyU.ss certU.ss certCA.ss certP1.ss keyP1.ss certP2.ss keyP2.ss \
		intP1.ss intP2.ss: testss CAss.cnf Uss.cnf P1ss.cnf P2ss.cnf FRC
	@echo "Generate and certify a test certificate"
	@sh ./testss
	@cat certCA.ss certU.ss > intP1.ss
	@cat certCA.ss certU.ss certP1.ss > intP2.ss

test_engine: $(ENGINETEST) FRC
	@echo "Manipulate the ENGINE structures"
	../util/shlib_wrap.sh ./$(ENGINETEST)

test_ssl: $(SSLTEST) testfipsssl testssl testsslproxy \
		keyU.ss certU.ss certCA.ss certP1.ss keyP1.ss \
		certP2.ss keyP2.ss intP1.ss intP2.ss FRC
	@echo "test SSL protocol"
	@if [ -n "$(FIPSCANLIB)" ]; then \
	  sh ./testfipsssl keyU.ss certU.ss certCA.ss; \
	fi
	../util/shlib_wrap.sh ./$(SSLTEST) -test_cipherlist
	@sh ./testssl keyU.ss certU.ss certCA.ss
	@sh ./testsslproxy keyP1.ss certP1.ss intP1.ss
	@sh ./testsslproxy keyP2.ss certP2.ss intP2.ss

test_ca: testca FRC
	@if ../util/shlib_wrap.sh ../apps/openssl no-rsa; then \
	  echo "skipping CA.sh test -- requires RSA"; \
	else \
	  echo "Generate and certify a test certificate via the 'ca' program"; \
	  sh ./testca; \
	fi

test_aes: #$(AESTEST) FRC
#	@echo "test Rijndael"
#	../util/shlib_wrap.sh ./$(AESTEST)

test_tsa: testtsa CAtsa.cnf FRC
	@if ../util/shlib_wrap.sh ../apps/openssl no-rsa; then \
	  echo "skipping testtsa test -- requires RSA"; \
	else \
	  sh ./testtsa; \
	fi

test_ige: $(IGETEST) FRC
	@echo "Test IGE mode"
	../util/shlib_wrap.sh ./$(IGETEST)

test_jpake: $(JPAKETEST) FRC
	@echo "Test JPAKE"
	../util/shlib_wrap.sh ./$(JPAKETEST)

test_cms: cms-test.pl smcont.txt smime-certs FRC
	@echo "CMS consistency test"
	$(PERL) ./cms-test.pl

smime-certs:
	(cd $(OPENSSLDIR)/test && tar cf - smime-certs) |tar xvf -

test_srp: $(SRPTEST) FRC
	@echo "Test SRP"
	../util/shlib_wrap.sh ./$(SRPTEST)

test_heartbeat: $(HEARTBEATTEST) FRC
	../util/shlib_wrap.sh ./$(HEARTBEATTEST)

FRC:
