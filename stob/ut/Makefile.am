UT_SRCDIR=@SRCDIR@/stob/ut

if LINUX
noinst_LTLIBRARIES    = libstob-ut.la
libstob_ut_la_SOURCES = adieu.c ad.c

noinst_PROGRAMS  = server client

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

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr io_[uk].[hc]

endif

distdir:
	cp $(UT_SRCDIR)/Makefile.am $(UT_SRCDIR)/Makefile.in \
           $(UT_SRCDIR)/server.c $(UT_SRCDIR)/io_fop_init.c  \
           $(UT_SRCDIR)/io_fop.h $(UT_SRCDIR)/client.c \
           $(UT_SRCDIR)/adieu.c $(UT_SRCDIR)/ad.c \
           $(UT_SRCDIR)/io.ff \
           @top_builddir@/@PACKAGE@-@VERSION@/stob/ut
