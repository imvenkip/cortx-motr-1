SUBDIRS    = . ut

lib_LTLIBRARIES = librpc.la
librpc_la_SOURCES  = pcache.c session_srv.c
librpc_la_LDFLAGS  = -release @LT_RELEASE@
librpc_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@ $(PTHREAD_LIBS) $(DB4LIBS)

INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir) -I@DB4SRC@/build_unix -I @DB4SRC@
