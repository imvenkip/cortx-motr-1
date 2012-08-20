SUBDIRS_BASE = m4 doc man patches

# Order is important until fid is reached. Fop needs all libs
# before it in the built list.
# Keep this list alphabetically sorted after "net".

SUBDIRS_LIBS =                     \
               lib                 \
               xcode/ff2c          \
               addb/ut             \
               fol                 \
               db                  \
               fid                 \
                                   \
               addb/addbff         \
               net                 \
               fop                 \
               addb                \
               balloc              \
               c2t1fs/linux_kernel \
               capa                \
               cfg                 \
               cob                 \
               console             \
               desim               \
               dtm                 \
               ioservice           \
               layout              \
               mw                  \
               nrs                 \
               pool                \
               reqh                \
               rm                  \
               rpc                 \
               sm                  \
               sns                 \
               stob                \
               udb                 \
               ut                  \
               xcode               \
               yaml2db

SUBDIR_IT = rpc/it

SUBDIR_UT = fop/ut fop/ut/long_lock lib/ut stob/ut layout/ut balloc/ut    \
	    db/ut fol/ut sns/ut desim/ut cob/ut console/ut colibri/ut     \
	    net/ut net/bulk_emulation/ut net/lnet/ut udb/ut capa/ut       \
	    ioservice/ut reqh/ut rm/ut rpc/ut sm/ut xcode/ut yaml2db/ut

SUBDIR_KERNEL = build_kernel_modules
SUBDIR_ST = console/st net/bulk_emulation/st net/lnet/st yaml2db/st

SUBDIR_UB =

SUBDIR_BIN = console/bin mds utils

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_IT)    \
             $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_KERNEL) $(SUBDIR_UB) \
             $(SUBDIR_BIN)

etags:
	find . -name '*.[ch]' | xargs etags

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog colibri.spec.in \
	     colibri.spec
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ \
	     automake.texi
ACLOCAL_AMFLAGS = -I m4

clean-local:
	rm -f c2.trace.*
	if test "x@ENABLE_COVERAGE@" = "x1"; \
	then \
		find . -name "*.gcno" | xargs rm -f; \
	fi
