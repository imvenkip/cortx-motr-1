io_u.h io_u.c: io.ff \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

noinst_LTLIBRARIES    = libstob-ut.la
libstob_ut_la_SOURCES = io_u.h io_u.c adieu.c ad.c stobio.c

noinst_PROGRAMS       = server client stobutil

server_SOURCES   = server.c io_fop.h
server_LDFLAGS   = -pthread
server_LDADD     = $(top_builddir)/colibri/libcolibri.la

client_SOURCES   = client.c io_fop.h
client_LDFLAGS   = -pthread
client_LDADD     = $(top_builddir)/colibri/libcolibri.la

stobutil_SOURCES = stobutil.c
stobutil_LDADD   = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd @SRCDIR@/stob/ut ; \
	rm -fr io_[uk].[hc]
