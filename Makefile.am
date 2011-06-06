SUBDIRS_BASE = m4 doc man patches

# Order is important until fop is reached. Fop needs all libs before it in the
# list built in order to use fop2c
# addb_pseudo is used only for build fop2c, with which full addb can be built
SUBDIRS_LIBS = \
               lib \
               addb/addb_pseudo \
               fol \
               db \
               fid \
               fop \ # keep this list alphabetically sorted after this point.

               addb \
               balloc \
               capa \
               cob \
               desim \
               dtm \
               ioservice \
               layout \
               mw \
               net \
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
SUBDIR_UT = \ # keep this list alphabetically sorted
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
               rpc/ut \
               sns/ut \
               stob/ut \
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
