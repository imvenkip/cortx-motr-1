# control verbosity level of make depending on 'V' command-line argument and
# --enable-silent-rules option of configure script
make_verbose   = $(make_verbose_$(V))
make_verbose_  = $(make_verbose_$(AM_DEFAULT_VERBOSITY))
make_verbose_0 = --no-print-directory

MAKEFLAGS = $(make_verbose)

SUBDIRS_BASE = m4 doc man patches

# xcode/ff2c must be built first, because it is used by other modules to compile
# their .ff files. net/ and addb/addbff must be built next, because their .ff
# files are used by other modules. The rest can be built in an arbitrary order.
# Keep the rest sorted lexicographically.

SUBDIRS_LIBS = \
               xcode/ff2c \
               addb/addbff \
               net \
               \
               addb \
               c2t1fs/linux_kernel \
               balloc \
               capa \
               cfg \
               cob \
               console \
               db \
               desim \
               dtm \
               fid \
               fol \
               fop \
               mdservice \
               mdstore \
               ioservice \
               layout \
               lib \
               mw \
               pool \
               reqh \
               rpc \
               sm \
               sns \
               stob \
               udb \
               ut \
               xcode \
               yaml2db
SUBDIR_IT = rpc/it

SUBDIR_UT = addb/ut \
            balloc/ut \
            c2t1fs/linux_kernel/ut \
            capa/ut \
            cob/ut \
            colibri/ut \
            console/ut \
            desim/ut \
            db/ut \
            fol/ut \
            fop/ut \
            fop/ut/long_lock \
            ioservice/ut \
            mdservice/ut \
            layout/ut \
            lib/ut \
            net/bulk_emulation/ut \
            net/lnet/ut \
	    net/test/ut \
            net/ut \
            reqh/ut \
            rpc/ut \
            sm/ut \
            sns/ut \
            stob/ut \
            udb/ut \
            xcode/ut \
            yaml2db/ut

SUBDIR_KERNEL = build_kernel_modules

SUBDIR_ST = c2t1fs/linux_kernel/st \
            console/st \
            net/bulk_emulation/st \
            net/lnet/st \
            yaml2db/st

SUBDIR_UB =

SUBDIR_BIN = console/bin net/test/user_space utils

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
