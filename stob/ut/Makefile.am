INCLUDES         = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix -I@DBSRC@

if LINUX
noinst_PROGRAMS  = server utadieu client

io_u.h io_u.c: io.ff \
                  $(top_builddir)/colibri/libcolibri.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -uk $<

server_SOURCES   = io_u.h io_u.c server.c io_fop_init.c io_fop.h
server_LDFLAGS   = -pthread
server_LDADD     = $(top_builddir)/colibri/libcolibri.la

client_SOURCES   = client.c io_fop_init.c io_fop.h io_u.h io_u.c 
client_LDFLAGS   = -pthread
client_LDADD     = $(top_builddir)/colibri/libcolibri.la

utadieu_SOURCES  = adieu.c
utadieu_LDFLAGS  = -pthread
utadieu_LDADD    = $(top_builddir)/colibri/libcolibri.la

clean-local:
	cd @SRCDIR@/stob/ut ; \
	rm -fr io_[uk].[hc]

endif
