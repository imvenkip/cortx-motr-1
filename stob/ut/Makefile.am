UT_SRCDIR=@SRCDIR@/stob/ut

noinst_LTLIBRARIES    = libstob-ut.la
libstob_ut_la_SOURCES = adieu.c ad.c stobio.c

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
