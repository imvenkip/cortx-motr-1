SUBDIRS    = . ut

lib_LTLIBRARIES            = libcolibri-stob.la
libcolibri_stob_la_SOURCES  = stob.c linux.c linux_adieu.c
libcolibri_stob_la_LDFLAGS  = -release @LT_RELEASE@
libcolibri_stob_la_LIBADD   = $(top_builddir)/lib/libc2.la @LIBS@

INCLUDES                   = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DB4SRC@/build_unix

#noinst_PROGRAMS   = server
#server_SOURCES   = main.c
#server_LDFLAGS   = @LDFLAGS@ -pthread
#server_LDADD     = $(top_builddir)/lib/libc2.la $(top_builddir)/sm/libcolibri-sm.la $(top_builddir)/../db4/build_unix/libdb-4.8.la @AIO_LIBS@ libcolibri-stob.la