UT_SRCDIR=@SRCDIR@/stob/ut

io_u.h io_u.c: io.ff \
                  $(top_builddir)/fop/rt/libc2rt.la \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

noinst_LTLIBRARIES    = libstob-ut.la
libstob_ut_la_SOURCES = adieu.c ad.c stobio.c io_u.c io_u.h

noinst_PROGRAMS       = server client

server_SOURCES   = server.c io_fop.h
server_LDFLAGS   = -pthread
server_LDADD     = $(top_builddir)/colibri/libcolibri.la

client_SOURCES   = client.c io_fop.h
client_LDFLAGS   = -pthread
client_LDADD     = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr io_[uk].[hc]
