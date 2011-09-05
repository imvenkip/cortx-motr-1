UT_SRCDIR=@SRCDIR@/rpc/ut
INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../
#noinst_PROGRAMS   = rclient rserver
noinst_PROGRAMS   = formation onwire_ut
#noinst_LTLIBRARIES       = librpccore-ut.la

#rclient_SOURCES   = rpc-client.c
#rclient_LDFLAGS   = -pthread
#rclient_LDADD     = $(top_builddir)/colibri/libcolibri.la

#rserver_SOURCES   = rpc-server.c
#rserver_LDFLAGS   = -pthread
#rserver_LDADD     = $(top_builddir)/colibri/libcolibri.la
#noinst_LTLIBRARIES   = libformation-ut.la
#libformation_ut_la_SOURCES   = formation-ut.c

io_fop_init.c io_u.h io_u.c:
	ln -sf $(top_builddir)/stob/ut/io_fop_init.c io_fop_init.c
	ln -sf $(top_builddir)/stob/ut/io_u.h io_u.h
	ln -sf $(top_builddir)/stob/ut/io_u.c io_u.c

formation_SOURCES   = io_u.h io_u.c io_fop_init.c formation-ut.c main.c
formation_LDFLAGS   = -pthread
formation_LDADD     = $(top_builddir)/colibri/libcolibri.la

test_u.h test_u.c : test.ff \
	$(top_builddir)/fop/rt/libc2rt.la \
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

onwire_ut_SOURCES     = test_u.h test_u.c onwire_ut.c
onwire_ut_LDADD       = $(top_builddir)/colibri/libcolibri.la


clean-local:
	rm -f io_fop_init.c \
		rm -f io_u.*
