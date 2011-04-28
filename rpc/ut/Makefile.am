UT_SRCDIR=@SRCDIR@/rpc/ut
INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I../
noinst_PROGRAMS   = rclient rserver
#noinst_LTLIBRARIES       = librpccore-ut.la

rclient_SOURCES   = rpc-client.c
rclient_LDFLAGS   = -pthread
rclient_LDADD     = $(top_builddir)/colibri/libcolibri.la

rserver_SOURCES   = rpc-server.c
rserver_LDFLAGS   = -pthread
rserver_LDADD     = $(top_builddir)/colibri/libcolibri.la

#librpccore_ut_la_SOURCES = rpccore_ut.c
#librpccore_ut_la_LIBADD  = $(top_builddir)/colibri/libcolibri.la
