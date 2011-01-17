INCLUDES        	= -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../
noinst_LTLIBRARIES   	= libnet-ut.la

net_u.h net_u.c: net.ff \
                  $(top_builddir)/colibri/libcolibri.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -uk $<

libnet_ut_la_SOURCES	= net_u.h net_u.c client.c net_fop_init.c net_fop.h 
libnet_ut_la_LIBADD 	= $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST              = net.ff

clean-local:
	cd $(top_builddir)/net/ut ; \
	rm -fr net_[uk].[hc]
