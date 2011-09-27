SUBDIRS_BASE = m4 doc man patches

# Order is important until fop is reached. Fop needs all libs before it in the
# list built in order to use fop2c
# addb_pseudo is used only for build fop2c, with which full addb can be built
SUBDIRS_LIBS = lib addb/addb_pseudo fol db yaml2db fid fop sm stob xdr \
	       net addb dtm nrs sns mw reqh layout pool balloc desim cob \
	       capa udb ioservice \
	       # rpc

SUBDIR_KERNEL = build_kernel_modules
SUBDIR_UT = fop/ut lib/ut stob/ut layout/ut balloc/ut db/ut \
            fol/ut sns/ut desim/ut cob/ut xdr/ut net/ut net/bulk_emulation/ut \
            udb/ut capa/ut reqh/ut yaml2db/ut #rpc/ut
SUBDIR_ST = net/st net/bulk_emulation/st yaml2db/st
SUBDIR_UB =
SUBDIR_BIN = mds utils

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_KERNEL) \
             $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_UB) $(SUBDIR_BIN)

etags:
	find . -name '*.[ch]' | xargs etags

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4
