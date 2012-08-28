# control verbosity level of make depending on 'V' command-line argument and
# --enable-silent-rules option of configure script
make_verbose   = $(make_verbose_$(V))
make_verbose_  = $(make_verbose_$(AM_DEFAULT_VERBOSITY))
make_verbose_0 = --no-print-directory

MAKEFLAGS = $(make_verbose)

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
	    ioservice/ut reqh/ut rpc/ut sm/ut xcode/ut yaml2db/ut

SUBDIR_KERNEL = build_kernel_modules
SUBDIR_ST = console/st net/bulk_emulation/st net/lnet/st yaml2db/st

SUBDIR_UB =

SUBDIR_BIN = console/bin mds utils

SUBDIR_SCRIPT = scripts

SUBDIRS    = $(SUBDIRS_BASE) $(SUBDIRS_LIBS) colibri $(SUBDIR_IT)    \
             $(SUBDIR_UT) $(SUBDIR_ST) $(SUBDIR_KERNEL) $(SUBDIR_UB) \
             $(SUBDIR_BIN) $(SUBDIR_SCRIPT)

# BUILT_SOURCES automake variable is a standard way to execute commands
# before the main build commands (`make all`), for more information please
# refer to section "Built Sources" in automake documentation
BUILT_SOURCES = pre-all

# this is executed just before 'all' target
.PHONY: pre-all
pre-all: check-sources-prebuild

# this is executed just after 'all' target
all-local: check-sources-postbuild

check-sources-prebuild:
	@echo "Checking source files..."
	@$(top_srcdir)/scripts/check-include-guards --noisy $(top_srcdir)

check-sources-postbuild:
	@#$(top_srcdir)/scripts/check-include-guards $(top_srcdir)
	@$(top_srcdir)/scripts/check-include-guards --noisy $(top_srcdir)

etags:
	find . -name '*.[ch]' | xargs etags

EXTRA_DIST = autogen.sh README NEWS AUTHORS COPYING ChangeLog colibri.spec.in colibri.spec
ETAGS_ARGS = automake.in --lang=none \ --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi
ACLOCAL_AMFLAGS = -I m4

clean-local:
	rm -f c2.trace.*
	if test "x@ENABLE_COVERAGE@" = "x1"; \
	then \
		find . -name "*.gcno" | xargs rm -f; \
	fi
