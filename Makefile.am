SUBDIRS_BASE = m4 doc include man patches 
SUBDIRS_LIBS = lib sm addb fop stob net fol dtm nrs sns mw reqh layout # rpc
SUBDIR_KERNEL = build_kernel_modules
SUBDIR_BINARIES = mds utils
SUBDIR_UT = fop/ut lib/ut stob/ut layout/ut #net/ut rpc/ut 
SUBDIR_ST = 
SUBDIR_UB = 

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) $(SUBDIR_KERNEL) colibri \
             $(SUBDIR_BINARIES) $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_UB)

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4
