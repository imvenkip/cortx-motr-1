INCLUDES          = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix

noinst_PROGRAMS   = rclient rserver

rclient_SOURCES   = rpc-client.c
rclient_LDFLAGS   = -pthread
rclient_LDADD     = $(top_builddir)/colibri/libcolibri.la

rserver_SOURCES   = rpc-server.c
rserver_LDFLAGS   = -pthread
rserver_LDADD     = $(top_builddir)/colibri/libcolibri.la


