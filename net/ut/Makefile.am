noinst_LTLIBRARIES   	= libnet-ut.la
INCLUDES        	= -I. -I$(top_srcdir) -I$(top_srcdir)/include -I$(top_srcdir)/net

net_u.h net_u.c: net.ff \
                  $(top_builddir)/fop/rt/libc2rt.la \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

libnet_ut_la_SOURCES	= bulk_if.c \
                          net_u.h net_u.c net_fop_init.c net_fop.h \
                          buffer_pool_ut.c
libnet_ut_la_LIBADD     = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST              = net.ff

clean-local:
	cd @SRCDIR@/net/ut ; \
	rm -fr net_[uk].[hc]
