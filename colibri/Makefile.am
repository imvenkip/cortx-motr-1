colibridir                = $(includedir)/colibri/colibri
colibri_HEADERS           = init.h colibri_setup.h

SUBDIRS                   = .

sbin_PROGRAMS             = colibri_setup
colibri_setup_SOURCES     = cs_main.c colibri_setup.c colibri_setup.h \
                            cs_internal.h
colibri_setup_LDADD       = $(top_builddir)/colibri/libcolibri.la
colibri_setup_CFLAGS      = $(AM_CFLAGS)

EXTRA_DIST                = devices.sh DS-Reqh.gif

lib_LTLIBRARIES           = libcolibri.la
libcolibri_la_SOURCES     = init.h init.c colibri_setup.c colibri_setup.h
libcolibri_la_LDFLAGS     = -release @LT_RELEASE@ -pthread
libcolibri_la_LIBADD      = @AIO_LIBS@ @PTHREAD_LIBS@ @DB_LIBS@ @GALOIS_LIBS@ @YAML_LIBS@ @CUNIT_LIBS@ \
                            -lm -lncurses \
                            $(top_builddir)/addb/libcolibri-addb.la \
                            $(top_builddir)/addb/addbff/libcolibri-addbff.la \
                            $(top_builddir)/balloc/libcolibri-balloc.la \
			    $(top_builddir)/capa/libcolibri-capa.la \
                            $(top_builddir)/cfg/libcolibri-cfg.la \
                            $(top_builddir)/cob/libcolibri-cob.la \
                            $(top_builddir)/console/libcolibri-console.la \
                            $(top_builddir)/db/libcolibri-db.la \
                            $(top_builddir)/desim/libcolibri-desim.la \
                            $(top_builddir)/dtm/libcolibri-dtm.la \
                            $(top_builddir)/fid/libcolibri-fid.la \
                            $(top_builddir)/fol/libcolibri-fol.la \
                            $(top_builddir)/fop/libcolibri-fop.la \
                            $(top_builddir)/layout/libcolibri-layout.la \
                            $(top_builddir)/lib/libc2.la \
                            $(top_builddir)/net/libcolibri-net.la \
                            $(top_builddir)/net/bulk_emulation/libcolibri-net-bulkmem.la \
                            $(top_builddir)/net/lnet/libcolibri-lnet.la \
                            $(top_builddir)/pool/libcolibri-pool.la \
                            $(top_builddir)/reqh/libcolibri-reqh.la \
                            $(top_builddir)/sm/libcolibri-sm.la \
                            $(top_builddir)/sns/libcolibri-sns.la \
                            $(top_builddir)/stob/libcolibri-stob.la \
                            $(top_builddir)/udb/libcolibri-udb.la \
                            $(top_builddir)/ut/libcolibri-ut.la \
                            $(top_builddir)/ioservice/libcolibri-ioservice.la \
                            $(top_builddir)/mdstore/libcolibri-mdstore.la \
                            $(top_builddir)/mdservice/libcolibri-mdservice.la \
                            $(top_builddir)/rpc/libcolibri-rpc.la \
                            $(top_builddir)/xcode/libcolibri-xcode.la \
                            $(top_builddir)/xcode/ff2c/libcolibri-xcode-ff2c.la \
                            $(top_builddir)/yaml2db/libcolibri-yaml2db.la
