INCLUDES        	= -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../
noinst_LTLIBRARIES   	= libnet-ut.la
libnet_ut_la_SOURCES	= io_u.h io_u.c client.c io_fop_init.c io_fop.h 
libnet_ut_la_LIBADD 	= $(top_builddir)/colibri/libcolibri.la

io_u.h io_u.c: io.ff \
                  $(top_builddir)/colibri/libcolibri.la \
                  $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -uk $<


clean distclean:
	cd $(top_builddir)/net/ut ; \
	rm -fr io_[uk].[hc]
