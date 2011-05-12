SUBDIRS                   = . 

lib_LTLIBRARIES           = libcolibri.la
libcolibri_la_SOURCES     = init.h init.c
libcolibri_la_LDFLAGS     = -release @LT_RELEASE@ -pthread 
libcolibri_la_LIBADD      = @AIO_LIBS@ @PTHREAD_LIBS@ @DB_LIBS@ @GALOIS_LIBS@ @CUNIT_LIBS@ \
                            -lm -lncurses \
                            $(top_builddir)/addb/libcolibri-addb.la \
                            $(top_builddir)/balloc/libcolibri-balloc.la \
                            $(top_builddir)/capa/libcolibri-capa.la \
                            $(top_builddir)/cob/libcolibri-cob.la \
                            $(top_builddir)/db/libcolibri-db.la \
                            $(top_builddir)/desim/libcolibri-desim.la \
                            $(top_builddir)/dtm/libcolibri-dtm.la \
                            $(top_builddir)/fid/libcolibri-fid.la \
                            $(top_builddir)/fol/libcolibri-fol.la \
                            $(top_builddir)/fop/libcolibri-fop.la \
                            $(top_builddir)/layout/libcolibri-layout.la \
                            $(top_builddir)/lib/libc2.la \
                            $(top_builddir)/net/libcolibri-net.la \
                            $(top_builddir)/net/usunrpc/libcolibri-net-usunrpc.la \
                            $(top_builddir)/nrs/libcolibri-nrs.la \
                            $(top_builddir)/pool/libcolibri-pool.la \
                            $(top_builddir)/reqh/libcolibri-reqh.la \
                            $(top_builddir)/sm/libcolibri-sm.la \
                            $(top_builddir)/sns/libcolibri-sns.la \
                            $(top_builddir)/stob/libcolibri-stob.la \
                            $(top_builddir)/udb/libcolibri-udb.la \
                            $(top_builddir)/rm/libcolibri-rm.la \
                            $(top_builddir)/ioservice/libcolibri-ioservice.la

#                            $(top_builddir)/rpc/libcolibri-rpc.la


