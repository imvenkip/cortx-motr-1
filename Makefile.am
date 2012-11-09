################################################################################
#                              Quick start guide
################################################################################

#
# This is a main colibri makefile. It's responsible for building all user-space
# programs and libraries.
#
# It uses non-recursive make technique which is superior in many aspects to
# "recursive make". For more information on "non-recursive vs. recursive make",
# please refer to an excellent article "Recursive Make Considered Harmful" by
# Peter Miller - http://miller.emu.id.au/pmiller/books/rmch/
#
# A task of building kernel-space modules is delegated to the
# build_kernel_modules/Makefile.in makefile, which is automatically invoked by
# this makefile after user-space build is complete.
#
# This makefile uses 'include' directives of make program to source other
# sub-makefiles (Makefile.sub), which are located in subdirectories and
# describe what should be built in each particular subdir.
#
# Normally, sub-makefiles should contain only *_HEADERS, *_SOURCES, EXTRA_DIST,
# CLEANFILES, FF_FILES and XC_FILES variables, plus targets, which describe some
# local dependencies or do some local preparations (like creating symlinks and
# so on).
#
# All other primary variables (in automake sense), like *_PROGRAMS,
# *_LTLIBRARIES, *_CFLAGS, *_LDADD, _LIBADD and global targets should be defined
# in this main makefile.
#
# Colibri's public headers should be listed in *_HEADERS variables, while
# internal headers, which are not part of the public interface, should be listed
# in *_SOURCES variables.
#


################################################################################
#                            Automake configuration
################################################################################

# control verbosity level of make depending on 'V' command-line argument and
# --enable-silent-rules option of configure script
make_verbose   = $(make_verbose_$(V))
make_verbose_  = $(make_verbose_$(AM_DEFAULT_VERBOSITY))
make_verbose_0 = --no-print-directory

MAKEFLAGS = $(make_verbose)

# in order to suppress annoying automake warning "CFLAGS: non-POSIX variable name"
# -Wno-portability automake option should be set for this makefile, this is
# required for xcode generation
AUTOMAKE_OPTIONS = -Wno-portability

# required to properly rebuild aclocal.m4 macros on configure.ac or Makefile.am
# updates
ACLOCAL_AMFLAGS = -I m4

# default XXXFLAGS, used for preprocessing, compiling and linking for all
# user-space targets
#
# NOTE[1]: automake doesn't use these flags if per-target XXXFLAGS are defined,
#          so if one doesn't want to replace these default AM_XXXFLAGS with
#          per-target XXXFLAGS, but just to extend them, then AM_XXXFLAGS should
#          be appended to a per-target XXXFLAGS:
#
#              my_program_CFLAGS = --some-compiler-option $(AM_CFLAGS)
#
# NOTE[2]: XXXFLAGS variables provided by a user from command-line
#          (like `make CFLAGS=-Werror`) are always used implicitly in
#          automake's build rules, so there is no need to specify them directly
#          in AM_XXXFLAGS or per-target XXXFLAGS variables.
#
AM_CPPFLAGS  = @C2_CPPFLAGS@
AM_CFLAGS    = @C2_CFLAGS@
AM_LDFLAGS   = @C2_LDFLAGS@


################################################################################
#                               Global variables
################################################################################

# initialize variables, so values can be appended later with += if needed

bin_PROGRAMS       =
sbin_PROGRAMS      =
lib_LTLIBRARIES    =

noinst_PROGRAMS    =
noinst_LTLIBRARIES =

# FIXME: does anyone needs this or it can be removed completely?
#ETAGS_ARGS = automake.in --lang=none \
#             --regex=’/^@node[ \t]+\([^,]+\)/\1/’ automake.texi

EXTRA_DIST = autogen.sh colibri.spec.in
CLEANFILES =

# a list of auto-generated *_ff.[hc] files
FF_FILES =

# a list of auto-generated *_xc.[hc] files
XC_FILES =


################################################################################
#                     Internal programs required for build
################################################################################

#
# ff2c
#

FF2C = $(top_builddir)/xcode/ff2c/ff2c

noinst_PROGRAMS       += xcode/ff2c/ff2c
xcode_ff2c_ff2c_LDADD  = xcode/ff2c/libcolibri-xcode-ff2c.la

# a separate ff2c library is required in order to test it in UT
noinst_LTLIBRARIES    += xcode/ff2c/libcolibri-xcode-ff2c.la

include $(top_srcdir)/xcode/ff2c/Makefile.sub


################################################################################
#                                  Libraries
################################################################################

#
# libcolibri.so
#

lib_LTLIBRARIES                += colibri/libcolibri.la
colibri_libcolibri_la_LDFLAGS   = -release @LT_RELEASE@ -pthread $(AM_LDFLAGS)
colibri_libcolibri_la_LIBADD    = @MATH_LIBS@ @PTHREAD_LIBS@ @AIO_LIBS@ @RT_LIBS@ \
                                  @DB_LIBS@ @GALOIS_LIBS@ @YAML_LIBS@ @CUNIT_LIBS@

# TODO: remove CUnit dependency from libcolibri, move lib/ut.c to ut/ and
# libcolibri-ut

# install directory for public libcolibri headers
colibri_includedir             = $(includedir)/colibri

# these variables are populated in the Makefile.sub files of each colibri module,
# which are then included into this top-level makefile
nobase_colibri_include_HEADERS =
colibri_libcolibri_la_SOURCES  =

include $(top_srcdir)/addb/Makefile.sub
include $(top_srcdir)/addb/addbff/Makefile.sub
include $(top_srcdir)/balloc/Makefile.sub
include $(top_srcdir)/build_kernel_modules/Makefile.sub
include $(top_srcdir)/c2t1fs/Makefile.sub
include $(top_srcdir)/capa/Makefile.sub
include $(top_srcdir)/cfg/Makefile.sub
include $(top_srcdir)/cm/Makefile.sub
include $(top_srcdir)/cob/Makefile.sub
include $(top_srcdir)/colibri/Makefile.sub
include $(top_srcdir)/conf/Makefile.sub
include $(top_srcdir)/console/Makefile.sub
include $(top_srcdir)/db/Makefile.sub
include $(top_srcdir)/desim/Makefile.sub
include $(top_srcdir)/dtm/Makefile.sub
include $(top_srcdir)/fid/Makefile.sub
include $(top_srcdir)/fol/Makefile.sub
include $(top_srcdir)/fop/Makefile.sub
include $(top_srcdir)/ioservice/Makefile.sub
include $(top_srcdir)/layout/Makefile.sub
include $(top_srcdir)/lib/Makefile.sub
include $(top_srcdir)/mdservice/Makefile.sub
include $(top_srcdir)/mdstore/Makefile.sub
include $(top_srcdir)/mw/Makefile.sub
include $(top_srcdir)/net/Makefile.sub
include $(top_srcdir)/net/bulk_emulation/Makefile.sub
include $(top_srcdir)/net/lnet/Makefile.sub
include $(top_srcdir)/pool/Makefile.sub
include $(top_srcdir)/reqh/Makefile.sub
include $(top_srcdir)/rpc/Makefile.sub
include $(top_srcdir)/sm/Makefile.sub
include $(top_srcdir)/sns/Makefile.sub
include $(top_srcdir)/sns/repair/Makefile.sub
include $(top_srcdir)/stob/Makefile.sub
include $(top_srcdir)/udb/Makefile.sub
include $(top_srcdir)/xcode/Makefile.sub
include $(top_srcdir)/yaml2db/Makefile.sub

#
# libcolibri-ut.so
#

noinst_LTLIBRARIES          += ut/libcolibri-ut.la
#ut_libcolibri_ut_la_LIBADD   = @CUNIT_LIBS@

# conf/ut/Makefile.sub needs to use custom CPPFLAGS, so we need to initialize
# custom libcolibri-ut CPPFLAGS here
ut_libcolibri_ut_la_CPPFLAGS = $(AM_CPPFLAGS)

ut_libcolibri_ut_la_SOURCES  =

include $(top_srcdir)/addb/ut/Makefile.sub
include $(top_srcdir)/balloc/ut/Makefile.sub
include $(top_srcdir)/capa/ut/Makefile.sub
include $(top_srcdir)/cm/ut/Makefile.sub
include $(top_srcdir)/cob/ut/Makefile.sub
include $(top_srcdir)/colibri/ut/Makefile.sub
include $(top_srcdir)/conf/ut/Makefile.sub
include $(top_srcdir)/console/ut/Makefile.sub
include $(top_srcdir)/db/ut/Makefile.sub
include $(top_srcdir)/fol/ut/Makefile.sub
include $(top_srcdir)/fop/ut/Makefile.sub
include $(top_srcdir)/ioservice/ut/Makefile.sub
include $(top_srcdir)/layout/ut/Makefile.sub
include $(top_srcdir)/mdservice/ut/Makefile.sub
include $(top_srcdir)/lib/ut/Makefile.sub
include $(top_srcdir)/net/bulk_emulation/ut/Makefile.sub
include $(top_srcdir)/net/lnet/ut/Makefile.sub
include $(top_srcdir)/net/test/ut/Makefile.sub
include $(top_srcdir)/net/ut/Makefile.sub
include $(top_srcdir)/pool/ut/Makefile.sub
include $(top_srcdir)/reqh/ut/Makefile.sub
include $(top_srcdir)/rpc/ut/Makefile.sub
include $(top_srcdir)/sm/ut/Makefile.sub
include $(top_srcdir)/sns/ut/Makefile.sub
include $(top_srcdir)/sns/repair/ut/Makefile.sub
include $(top_srcdir)/stob/ut/Makefile.sub
include $(top_srcdir)/udb/ut/Makefile.sub
include $(top_srcdir)/ut/Makefile.sub
include $(top_srcdir)/xcode/ut/Makefile.sub
include $(top_srcdir)/yaml2db/ut/Makefile.sub

#
# libcolibri-net-test.so
#

noinst_LTLIBRARIES += net/test/libcolibri-net-test.la

net_test_libcolibri_net_test_la_SOURCES =

include $(top_srcdir)/net/test/Makefile.sub

#
# libcolibri-altogether.so
#

COLIBRI_ALTOGETHER_USER_C   = colibri/colibri_altogether_user.c
COLIBRI_ALTOGETHER_USER_SRC = $(filter %.c,$(colibri_libcolibri_la_SOURCES))

lib_LTLIBRARIES += colibri/libcolibri-altogether.la

colibri_libcolibri_altogether_la_SOURCES  = $(COLIBRI_ALTOGETHER_USER_C)
colibri_libcolibri_altogether_la_CPPFLAGS = $(AM_CPPFLAGS) \
                                            -UC2_INTERNAL \
                                            -DC2_INTERNAL=static \
                                            -UC2_EXTERN \
                                            -DC2_EXTERN=static

if ENABLE_DEBUG
colibri_libcolibri_altogether_la_CFLAGS   = $(AM_CFLAGS) \
                                            -Wno-unused-function
else
colibri_libcolibri_altogether_la_CFLAGS   = $(AM_CFLAGS) \
                                            -Wno-unused-function \
                                            -O3 -ffast-math
endif

colibri_libcolibri_altogether_la_LDFLAGS  = $(colibri_libcolibri_la_LDFLAGS)
colibri_libcolibri_altogether_la_LIBADD   = $(colibri_libcolibri_la_LIBADD)

CLEANFILES += $(COLIBRI_ALTOGETHER_USER_C)

################################################################################
#                          Unit tests and benchmarks
################################################################################

#
# balloc/ut
#

noinst_PROGRAMS                  += balloc/ut/format
balloc_ut_format_LDADD            = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/free
balloc_ut_free_LDADD              = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/dump_free_extent
balloc_ut_dump_free_extent_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/dump_group_desc
balloc_ut_dump_group_desc_LDADD   = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/dump_super_block
balloc_ut_dump_super_block_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/perf
balloc_ut_perf_LDADD              = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS                  += balloc/ut/mperf
balloc_ut_mperf_LDADD             = $(top_builddir)/colibri/libcolibri.la \
                                    $(top_builddir)/ut/libcolibri-ut.la

#
# c2t1fs/linux_kernel/ut
#

include $(top_srcdir)/c2t1fs/linux_kernel/ut/Makefile.sub

#
# db/ut
#

noinst_PROGRAMS   += db/ut/elist
db_ut_elist_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                     $(top_builddir)/ut/libcolibri-ut.la

#
# desim/ut
#

noinst_PROGRAMS            += desim/ut/net_test
desim_ut_net_test_LDADD     = $(top_builddir)/colibri/libcolibri.la \
                              $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS            += desim/ut/chs_test
desim_ut_chs_test_LDADD     = $(top_builddir)/colibri/libcolibri.la \
                              $(top_builddir)/ut/libcolibri-ut.la

noinst_PROGRAMS            += desim/ut/c2t1fs_test
desim_ut_c2t1fs_test_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                              $(top_builddir)/ut/libcolibri-ut.la

#
# layout/ut
#

noinst_PROGRAMS       += layout/ut/ldemo
layout_ut_ldemo_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                         $(top_builddir)/ut/libcolibri-ut.la

#
# net/lnet/ut
#

noinst_PROGRAMS              += net/lnet/ut/lut_helper
net_lnet_ut_lut_helper_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                                $(top_builddir)/ut/libcolibri-ut.la

#
# utils/ut and utils/ub
#

noinst_PROGRAMS += utils/ut
utils_ut_LDADD   = $(top_builddir)/ut/libcolibri-ut.la \
                   $(top_builddir)/colibri/libcolibri.la \
                   $(top_builddir)/net/test/libcolibri-net-test.la \
                   $(top_builddir)/xcode/ff2c/libcolibri-xcode-ff2c.la

#noinst_PROGRAMS += utils/ut-altogether
#utils_ut_altogether_SOURCES = $(utils_ut_SOURCES)
#utils_ut_altogether_LDADD   = $(top_builddir)/ut/libcolibri-ut.la \
#                              $(top_builddir)/colibri/libcolibri-altogether.la \
#                              $(top_builddir)/net/test/libcolibri-net-test.la \
#                              $(top_builddir)/xcode/ff2c/libcolibri-xcode-ff2c.la

noinst_PROGRAMS += utils/ub
utils_ub_LDADD   = $(top_builddir)/colibri/libcolibri.la \
                   $(top_builddir)/ut/libcolibri-ut.la

#noinst_PROGRAMS += utils/ub-altogether
#utils_ub_altogether_SOURCES = $(utils_ub_SOURCES)
#utils_ub_altogether_LDADD   = $(top_builddir)/colibri/libcolibri-altogether.la \
#                              $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/utils/Makefile.sub


################################################################################
#                    Integration and system test utilities
################################################################################

#
# rpc/it/rpcping
#

noinst_PROGRAMS      += rpc/it/rpcping
rpc_it_rpcping_LDADD  = $(top_builddir)/colibri/libcolibri.la  \
                        $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/rpc/it/Makefile.sub

#
# conf/st
#

noinst_PROGRAMS          += conf/st/objx2db
conf_st_objx2db_LDADD     = $(top_builddir)/colibri/libcolibri.la

noinst_PROGRAMS          += conf/st/confd_test
conf_st_confd_test_LDADD  = $(top_builddir)/colibri/libcolibri.la

noinst_PROGRAMS          += conf/st/confc_test
conf_st_confc_test_LDADD  = $(top_builddir)/colibri/libcolibri.la

#
# console/st
#

noinst_PROGRAMS         += console/st/server
console_st_server_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                           $(top_builddir)/ut/libcolibri-ut.la

#
# net/bulk_emulation/st
#

noinst_PROGRAMS                      += net/bulk_emulation/st/bulkping
net_bulk_emulation_st_bulkping_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                                        $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/net/bulk_emulation/st/Makefile.sub

#
# net/lnet/st
#

noinst_PROGRAMS            += net/lnet/st/lnetping
net_lnet_st_lnetping_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                              $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/net/lnet/st/Makefile.sub

#
# net/test
#

noinst_PROGRAMS                 += net/test/user_space/ntc
net_test_user_space_ntc_LDADD    = $(top_builddir)/colibri/libcolibri.la \
                                   $(top_builddir)/net/test/libcolibri-net-test.la

noinst_PROGRAMS                 += net/test/user_space/ntn
net_test_user_space_ntn_LDADD    = $(top_builddir)/colibri/libcolibri.la \
                                   $(top_builddir)/net/test/libcolibri-net-test.la

include $(top_srcdir)/net/test/user_space/Makefile.sub

#
# sns/repair/st
#

noinst_PROGRAMS            += sns/repair/st/repair
sns_repair_st_repair_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                              $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/sns/repair/st/Makefile.sub

#
# yaml2db/st
#

noinst_PROGRAMS          += yaml2db/st/yaml2db
yaml2db_st_yaml2db_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                            $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/yaml2db/st/Makefile.sub

#
# utils/ploss
#

noinst_PROGRAMS += utils/ploss/ploss_server
noinst_PROGRAMS += utils/ploss/ploss_client

include $(top_srcdir)/utils/ploss/Makefile.sub

#
# c2t1fs/linux_kernel/st
#

include $(top_srcdir)/c2t1fs/linux_kernel/st/Makefile.sub


################################################################################
#                               Public utilities
################################################################################

#
# colibri/colibri_setup
#

sbin_PROGRAMS               += colibri/colibri_setup
colibri_colibri_setup_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                               $(top_builddir)/ut/libcolibri-ut.la

#
# console/bin/c2console
#

bin_PROGRAMS                += console/bin/c2console
console_bin_c2console_LDADD  = $(top_builddir)/colibri/libcolibri.la \
                               $(top_builddir)/ut/libcolibri-ut.la

include $(top_srcdir)/console/bin/Makefile.sub


################################################################################
#                                     Misc
################################################################################

include $(top_srcdir)/scripts/Makefile.sub
include $(top_srcdir)/patches/Makefile.sub
include $(top_srcdir)/doc/Makefile.sub


################################################################################
#                                 Build rules
################################################################################

#
# ff files
#

# rules for *_ff.[ch] files, which are automatically generated from
# corresponding *.ff files by ff2c compiler.

# hide actual ff2c build command in silent make mode (V=0) and display it
# otherwise (V=1); take into account default verbosity level in configure
# (controlled by --enable-silent-rules option)
ff2c_verbose   = $(ff2c_verbose_$(V))
ff2c_verbose_  = $(ff2c_verbose_$(AM_DEFAULT_VERBOSITY))
ff2c_verbose_0 = @echo "  FF2C  " $<;

#$(top_srcdir)/%_ff.h $(top_srcdir)/%_ff.c: $(top_srcdir)/%.ff $(FF2C)
%_ff.h %_ff.c: %.ff $(FF2C)
	$(ff2c_verbose)$(FF2C) $<

#
# xc files
#

# rules for *_xc.[hc] files, which are automatically generated from
# corresponding *.h files.

# *_xc.[ch] files contain generated xcode data structures for the given C
# structures from *.h files. For more information about xcode please refer to
# xcode/xcode.h and xcode/gccxml2xcode documentation.

# hide actual gccxml/gccxml2xcode build commands in silent make mode (V=0) and
# display them otherwise (V=1); take into account default verbosity level in
# configure (controlled by --enable-silent-rules option)
gccxml_verbose   = $(gccxml_verbose_$(V))
gccxml_verbose_  = $(gccxml_verbose_$(AM_DEFAULT_VERBOSITY))
gccxml_verbose_0 = @echo " GCCXML " $@;

gccxml2xcode_verbose   = $(gccxml2xcode_verbose_$(V))
gccxml2xcode_verbose_  = $(gccxml2xcode_verbose_$(AM_DEFAULT_VERBOSITY))
gccxml2xcode_verbose_0 = @echo " GCCXML2XC " $@;

# gccxml doesn't like -Werror and --coverage options so we need to remove them
# from CFLAGS

%_xc.h %_xc.c: %.h
	$(gccxml_verbose)$(GCCXML) \
		$(filter-out -Werror --coverage, $(CPPFLAGS) $(CFLAGS) $(AM_CPPFLAGS) $(AM_CFLAGS)) \
		-fxml=$(<:.h=.gccxml) $<
	$(gccxml2xcode_verbose)$(top_srcdir)/xcode/gccxml2xcode -i $(<:.h=.gccxml)

#
# Pre-build targets
#

# BUILT_SOURCES automake variable is a standard way to execute commands
# before the main build commands (`make all`), for more information please
# refer to section "Built Sources" in automake documentation
BUILT_SOURCES = pre-all

# this is executed just before 'all' target
.PHONY: pre-all
pre-all: check-sources-prebuild generate-files

.PHONY: check-sources-prebuild
check-sources-prebuild:
	@#echo "Checking source files..."
	@$(top_srcdir)/scripts/check-include-guards --noisy $(top_srcdir)

.PHONY: generate-files
generate-files: generate-xc-files generate-ff-files

.PHONY: generate-ff-files
generate-ff-files: $(FF_FILES)

.PHONY: generate-xc-files
generate-xc-files: $(XC_FILES)

#
# Post-build targets
#

# this is executed just after 'all' target
all-local: build-kernel-modules check-sources-postbuild

.PHONY: check-sources-postbuild
check-sources-postbuild:
	@$(top_srcdir)/scripts/check-include-guards $(top_srcdir)

.PHONY: build-kernel-modules
build-kernel-modules:
	@$(MAKE) -C $(top_builddir)/build_kernel_modules $(AM_MAKEFLAGS)

#
# Convenient targets for separate components
#

# all-user

.PHONY: all-user
all-user: pre-all
	@$(MAKE) $(AM_MAKEFLAGS) __all-user

.PHONY: __all-user
__all-user: config.h Makefile $(LTLIBRARIES) $(PROGRAMS) $(HEADERS)

# all-kernel

.PHONY: all-kernel
all-kernel: pre-all
	@$(MAKE) $(AM_MAKEFLAGS) build-kernel-modules

# libcolibri

.PHONY: libcolibri
libcolibri: pre-all
	@$(MAKE) $(AM_MAKEFLAGS) __libcolibri

.PHONY: __libcolibri
__libcolibri: config.h Makefile colibri/libcolibri.la $(HEADERS)

# libcolibri-altogether

$(COLIBRI_ALTOGETHER_USER_C): $(COLIBRI_ALTOGETHER_USER_SRC)
	@echo "/* This file is autogenerated, don't edit. */" > $(COLIBRI_ALTOGETHER_USER_C)
	@for f in $^; do \
		echo "#include \"$$f\"" >> $(COLIBRI_ALTOGETHER_USER_C); \
	done

.PHONY: libcolibri-altogether
libcolibri-altogether: pre-all
	@$(MAKE) $(AM_MAKEFLAGS) __libcolibri-altogether

.PHONY: __libcolibri-altogether
__libcolibri-altogether: config.h Makefile colibri/libcolibri-altogether.la $(HEADERS)

# libcolibri-ut

.PHONY: libcolibri-ut
libcolibri-ut: pre-all
	@$(MAKE) $(AM_MAKEFLAGS) __libcolibri-ut

.PHONY: __libcolibri-ut
__libcolibri-ut: config.h Makefile ut/libcolibri-ut.la $(HEADERS)

#
# Clean targets
#

clean-local: clean-kernel-modules
	rm -f c2.trace.*
	if test x@ENABLE_COVERAGE@ = xyes; then \
		find . -name "*.gcno" | xargs rm -f; \
	fi

.PHONY: clean-kernel-modules
clean-kernel-modules:
	$(MAKE) -C $(top_builddir)/build_kernel_modules $(AM_MAKEFLAGS) clean

.PHONY: clean-doc
clean-doc:
	rm -fr doc/html

#
# Documentation
#

.PHONY: doc-doxygen
doc-doxygen:
	@$(DOXYGEN) doc/Doxyfile

.PHONY: doc
doc: doc-doxygen

#
# Misc targets
#

.PHONY: etags
etags:
	find . -name '*.[ch]' | xargs etags

.PHONY: list-programs
list-programs:
	@for p in $(PROGRAMS); do \
		echo "$$p"; \
	done | sort

.PHONY: list-libs
list-libs:
	@for l in $(LTLIBRARIES); do \
		echo "$$l"; \
	done | sort

.PHONY: help
help:
	@echo 'Build targets:'
	@echo '  all             - build programs, libraries, kernel modules, etc.,'
	@echo '                    this is a default, same as just `make`'
	@echo '  all-user        - build only user-space libraries and programs'
	@echo '  all-kernel      - build only kernel modules'
	@echo ''
	@echo '  libcolibri      - build only libcolibri library'
	@echo '  libcolibri-ut   - build only libcolibri-ut library'
	@echo ''
	@echo '  libcolibri-altogether - build only libcolibri library'
	@echo ''
	@echo '  dir/file[.o]    - build specified target only'
	@echo ''
	@echo 'Cleaning targets:'
	@echo '  clean           - delete from the build tree files created by `make all`'
	@echo '  distclean       - same as clean, but additionally delete anything,'
	@echo '                    created by ./configure'
	@echo ''
	@echo 'Documentation:'
	@echo '  doc             - generate all documentation'
	@echo '  doc-doxygen     - generate doxygen documentation'
	@echo ''
	@echo 'Distribution targets:'
	@echo '  install         - install what needs to be installed, copying the'
	@echo '                    files from the build tree to system-wide directories'
	@echo '  uninstall       - the opposite of `make install` - erase installed files'
	@echo '                    (this needs to be run from the same build tree that'
	@echo '                     was installed)'
	@echo '  dist            - recreate package-version.tar.gz from all source files'
	@echo ''
	@echo 'Information:'
	@echo '  list-programs   - list all executables built in colibri'
	@echo '  list-libs       - list libraries built in colibri'
