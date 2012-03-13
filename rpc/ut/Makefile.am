INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../

#noinst_PROGRAMS   = rclient rserver
# noinst_PROGRAMS   = onwire_ut

#noinst_LTLIBRARIES = librpccore-ut.la
noinst_LTLIBRARIES = librpclib-ut.la libonwire-ut.la

#rclient_SOURCES   = rpc-client.c
#rclient_LDFLAGS   = -pthread
#rclient_LDADD     = $(top_builddir)/colibri/libcolibri.la

#rserver_SOURCES   = rpc-server.c
#rserver_LDFLAGS   = -pthread
#rserver_LDADD     = $(top_builddir)/colibri/libcolibri.la

librpclib_ut_la_SOURCES = rpclib_ut.c ../it/ping_fop.c ../it/ping_fop_u.c ../it/ping_fom.c \
				service.c

librpclib_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

#noinst_LTLIBRARIES = libonwire-ut.la
test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la \
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

libonwire_ut_la_SOURCES     = test_u.h test_u.c onwire_ut.c

EXTRA_DIST = test.ff

clean-local:
	cd @SRCDIR@/rpc/ut ; \
	rm -fr test_[uk].[ch]
