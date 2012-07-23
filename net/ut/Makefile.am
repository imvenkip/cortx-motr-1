noinst_LTLIBRARIES   	= libnet-ut.la
INCLUDES        	= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include -iquote $(top_srcdir)/net

net.h net.c: net.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

libnet_ut_la_SOURCES	= bulk_if.c                             \
                          net.h net.c net_fop_init.c net_fop.h  \
                          buffer_pool_ut.c
libnet_ut_la_LIBADD     = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST              = net.ff tm_provision_ut.c

clean-local:
	cd @SRCDIR@/net/ut ; \
	rm -f net.[hc]
