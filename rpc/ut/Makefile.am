UT_SRCDIR=@SRCDIR@/rpc/ut

INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../

#noinst_PROGRAMS   = rclient rserver
noinst_PROGRAMS   = onwire_ut

#noinst_LTLIBRARIES = librpccore-ut.la
noinst_LTLIBRARIES = librpc-helper-ut.la

#rclient_SOURCES   = rpc-client.c
#rclient_LDFLAGS   = -pthread
#rclient_LDADD     = $(top_builddir)/colibri/libcolibri.la

#rserver_SOURCES   = rpc-server.c
#rserver_LDFLAGS   = -pthread
#rserver_LDADD     = $(top_builddir)/colibri/libcolibri.la

librpc_helper_ut_la_SOURCES = rpc_helper_ut.c ../it/ping_fop.c ../it/ping_fop_u.c ../it/ping_fom.c

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la \
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

onwire_ut_SOURCES     = test_u.h test_u.c onwire_ut.c
onwire_ut_LDADD       = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr test_[uk].[ch]
