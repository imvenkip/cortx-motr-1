SUBDIRS      = ploss

bin_PROGRAMS = ut ub

libstob_ut   = $(top_builddir)/stob/ut/libstob-ut.la

ut_SOURCES   = ut_main.c common.c common.h
ut_LDFLAGS   = -pthread
ut_LDADD     = $(top_builddir)/colibri/libcolibri.la \
               $(top_builddir)/lib/ut/libc2-ut.la $(libstob_ut) \
               $(top_builddir)/cob/ut/libcob-ut.la     \
               $(top_builddir)/db/ut/libdb-ut.la     \
               $(top_builddir)/cm/ut/libcmcp-ut.la     \
               $(top_builddir)/console/ut/libconsole-ut.la     \
               $(top_builddir)/fol/ut/libfol-ut.la   \
               $(top_builddir)/fop/ut/libfop-ut.la   \
               $(top_builddir)/layout/ut/liblayout-ut.la \
               $(top_builddir)/sns/ut/libparity-ut.la \
               $(top_builddir)/sns/repair/ut/libsns-repair-ut.la \
               $(top_builddir)/sns/repair/ut/libxform-ut.la \
               $(top_builddir)/net/ut/libnet-ut.la \
               $(top_builddir)/net/bulk_emulation/ut/libnet-bulkem-ut.la \
               $(top_builddir)/net/lnet/ut/liblnet-ut.la \
               $(top_builddir)/net/test/ut/libnet-test-ut.la \
               $(top_builddir)/udb/ut/libudb-ut.la \
	       $(top_builddir)/capa/ut/libcapa-ut.la \
               $(top_builddir)/sm/ut/libsm-ut.la \
               $(top_builddir)/xcode/ut/libxcode-ut.la \
               $(top_builddir)/reqh/ut/libreqh-ut.la \
               $(top_builddir)/colibri/ut/libcs-ut.la \
               $(top_builddir)/rpc/ut/librpc-ut.la \
               $(top_builddir)/pool/ut/libpool-ut.la \
               $(top_builddir)/ioservice/ut/libioservice-ut.la \
               $(top_builddir)/mdservice/ut/libmdservice-ut.la \
               $(top_builddir)/yaml2db/ut/libyaml2db-ut.la \
	       $(top_builddir)/addb/ut/libaddb-ut.la \
	       $(top_builddir)/balloc/ut/libballoc-ut.la \
	       $(top_builddir)/fop/ut/long_lock/libfop-lock-ut.la

ub_SOURCES   = ub_main.c common.c common.h
ub_LDFLAGS   = -pthread
ub_LDADD     = $(top_builddir)/colibri/libcolibri.la \
               $(top_builddir)/lib/ut/libc2-ut.la $(libstob_ut) \
               $(top_builddir)/db/ut/libdb-ut.la     \
               $(top_builddir)/cob/ut/libcob-ut.la     \
               $(top_builddir)/fol/ut/libfol-ut.la   \
               $(top_builddir)/sns/ut/libparity-ut.la

EXTRA_DIST   = ut.sh.in linux_kernel/main.c linux_kernel/ut.sh.in  \
	       linux_kernel/c2ctl.c linux_kernel/finject_debugfs.c \
	       linux_kernel/finject_debugfs.h
