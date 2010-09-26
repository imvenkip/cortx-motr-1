if LINUX
noinst_LTLIBRARIES    = libstob-ut.la
libstob_ut_la_SOURCES = adieu.c ad.c

noinst_PROGRAMS  = server client

io_u.h io_u.c: io.ff \
                  $(top_builddir)/colibri/libcolibri.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -uk $<

server_SOURCES   = server.c io_fop_init.c io_fop.h
server_LDFLAGS   = -pthread
server_LDADD     = $(top_builddir)/colibri/libcolibri.la

client_SOURCES   = client.c io_fop_init.c io_fop.h 
client_LDFLAGS   = -pthread
client_LDADD     = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd @SRCDIR@/stob/ut ; \
	rm -fr io_[uk].[hc]

endif
