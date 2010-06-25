SUBDIRS              = . 

lib_LTLIBRARIES      = libcolibri.la
libcolibri_la_SOURCES     = init.h init.c
libcolibri_la_LDFLAGS     = -release @LT_RELEASE@ -pthread
libcolibri_la_LIBADD      = @AIO_LIBS@ @DBLIBS@ @PTHREAD_LIBS@ \
                            $(top_builddir)/lib/libc2.la \
                            $(top_builddir)/sm/libcolibri-sm.la \
                            $(top_builddir)/stob/libcolibri-stob.la \
                            $(top_builddir)/net/libcolibri-net.la \
                            $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la \
                            $(top_builddir)/fop/libcolibri-fop.la \
                            $(top_builddir)/reqh/libcolibri-reqh.la \
                            $(top_builddir)/nrs/libcolibri-nrs.la \
                            $(top_builddir)/fol/libcolibri-fol.la \
                            $(top_builddir)/sns/libcolibri-sns.la \
                            $(top_builddir)/addb/libcolibri-addb.la \
                            $(top_builddir)/rpc/libcolibri-rpc.la

INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include -I@DBSRC@/build_unix 

