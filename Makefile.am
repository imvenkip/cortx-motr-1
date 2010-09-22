SUBDIRS_BASE = m4 doc man patches 
SUBDIRS_LIBS = lib sm addb fop stob net fol fid dtm nrs sns mw reqh layout \
               pool balloc db # rpc
SUBDIR_KERNEL = build_kernel_modules
SUBDIR_BINARIES = mds utils
SUBDIR_UT = fop/ut lib/ut stob/ut layout/ut balloc/ut db/ut \
            fol/ut #net/ut rpc/ut 
SUBDIR_ST = 
SUBDIR_UB = 
SUBDIR_BIN = utils mds

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_KERNEL) \
             $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_UB) $(SUBDIR_BIN)

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4
