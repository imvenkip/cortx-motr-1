INCLUDES          = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote ../

noinst_LTLIBRARIES = librpc-ut.la

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la \
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

librpc_ut_la_SOURCES = rpclib_ut.c \
			  ../it/ping_fop.c \
			  ../it/ping_fop_u.c \
			  ../it/ping_fom.c \
			  service.c \
			  test_u.h test_u.c \
			  onwire_ut.c \
			  formation2.c

librpc_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST = test.ff

clean-local:
	cd @SRCDIR@/rpc/ut ; \
	rm -fr test_[uk].[ch]
