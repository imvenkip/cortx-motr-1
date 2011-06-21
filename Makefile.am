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
               xdr

SUBDIR_KERNEL = build_kernel_modules
# keep this list alphabetically sorted
SUBDIR_UT = \
               balloc/ut \
               capa/ut \
               cob/ut \
               db/ut \
               desim/ut \
               fol/ut \
               fop/ut \
               layout/ut \
               lib/ut \
               net/bulk_emulation/ut \
               net/st \
               net/ut \
               sns/ut \
               stob/ut \
               rpc/ut \
               udb/ut \
               xdr/ut

SUBDIR_ST =
SUBDIR_UB =

SUBDIR_BIN = mds utils

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_KERNEL) \
             $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_UB) $(SUBDIR_BIN)

etags:
	find . -name '*.[ch]' | xargs etags

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4
