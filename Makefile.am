SUBDIRS_BASE = m4 doc man patches

# Order is important until fop/rt is reached. Fop needs all libs
# before it in the list built in order to use fop2c addb_pseudo is
# used only for build fop2c, with which full addb can be built. Keep
# this list alphabetically sorted after "net".

SUBDIRS_LIBS = \
               lib \
               addb/addb_pseudo \
               fol \
               db \
               fid \
               fop/rt \
               \
               net \
               fop \
               addb \
               balloc \
               capa \
               cfg \
               cob \
               desim \
               dtm \
               ioservice \
               layout \
               mw \
               nrs \
               pool \
               reqh \
               rpc \
               sm \
               sns \
               stob \
               udb \
               xcode \
               xdr
SUBDIR_IT = rpc/it
SUBDIR_KERNEL = build_kernel_modules
SUBDIR_UT = fop/ut lib/ut stob/ut layout/ut balloc/ut db/ut \
            fol/ut sns/ut desim/ut cob/ut net/ut net/bulk_emulation/ut \
            udb/ut capa/ut ioservice/ut reqh/ut rpc/ut sm/ut xcode/ut xdr/ut
SUBDIR_ST = net/st net/bulk_emulation/st

SUBDIR_UB =

SUBDIR_BIN = mds utils

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_IT) \
	     $(SUBDIR_KERNEL) $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_UB) \
	     $(SUBDIR_BIN)

etags:
	find . -name '*.[ch]' | xargs etags

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4

clean-local:
	if test "x@ENABLE_COVERAGE@" = "x1"; \
	then \
		find . -name "*.gcno" | xargs rm -f; \
	fi
