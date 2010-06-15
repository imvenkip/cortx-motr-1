SUBDIRS                    = . usunrpc ksunrpc ut

lib_LTLIBRARIES            = libcolibri-net.la
libcolibri_net_la_SOURCES  = net.c net.h net_xdr.c xdr.h net_utils.c net_cli.c net_srv.c connection.c
libcolibri_net_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_net_la_LIBADD   = $(top_builddir)/lib/libc2.la

INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include

