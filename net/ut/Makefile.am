noinst_LTLIBRARIES      = libnet-ut.la
INCLUDES                = -iquote . -iquote $(top_srcdir)       \
			  -iquote $(top_srcdir)/include -iquote \
			  $(top_srcdir)/net

libnet_ut_la_SOURCES	= bulk_if.c buffer_pool_ut.c

libnet_ut_la_LIBADD     = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST              = tm_provision_ut.c
