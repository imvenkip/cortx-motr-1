INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = rpcping

rpcping_SOURCES     = main.c $(top_builddir)/net/bulk_emulation/st/ping.c
rpcping_LDADD       = $(top_builddir)/colibri/libcolibri.la

