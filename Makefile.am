SUBDIRS    = m4 doc include man patches lib sm addb fop stob net rpc \
             fol dtm nrs sns \
             mds mw utils reqh build_kernel_modules \
             \
             fop/ut rpc/ut lib/ut stob/ut net/ut

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4
