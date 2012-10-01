AC_PREREQ([2.59])
AC_INIT([colibri], [0.1], [colibri@xyratex.com])
AC_CONFIG_SRCDIR([colibri/init.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AH_TEMPLATE([PACKAGE], [Package name.])
AH_TEMPLATE([VERSION], [Version of the package.])
AH_TEMPLATE([ENABLE_SYNC_ATOMIC], [Enable gcc built-in automic functions])
AH_TEMPLATE([ENABLE_DEBUG], [Enable debug info.])
AH_TEMPLATE([ENABLE_FAULT_INJECTION], [Enable fault injection.])
AH_TEMPLATE([ENABLE_IMMEDIATE_TRACE], [Enable immediate tracing.])
AH_TEMPLATE([HAVE_JOURNAL_ABORT], [Have journal_abort() function])
AH_TEMPLATE([HAVE_FILE_READV], [Have fops->readv function])
AH_TEMPLATE([HAVE_FILE_AIO_READ], [Have fops->aio_read function])
AH_TEMPLATE([HAVE_INODE_BLKSIZE], [Have inode->i_blksize field])
AH_TEMPLATE([HAVE_WRITE_BEGIN_END], [Have .begin_write/end_write])
AH_TEMPLATE([HAVE_SENDFILE], [Have .sendfile])
AH_TEMPLATE([HAVE_MALLINFO], [Have mallinfo() function])
AH_TEMPLATE([HAVE_MALLOC_SIZE], [Have malloc_size() function])
AH_TEMPLATE([HAVE_BACKTRACE], [Have backtrace(3) function])

COLIBRI_MAJOR_VERSION=0
COLIBRI_MINOR_VERSION=1

PACKAGE=colibri
VERSION=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION

AM_INIT_AUTOMAKE([nostdinc silent-rules])
AM_SILENT_RULES([yes])

LT_RELEASE=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION
AC_SUBST([LT_RELEASE])

AC_DEFUN([COLIBRI_LINUX_CHECK_SYMBOL],
        AC_MSG_CHECKING([for symbols file])
        if test -f $LINUX_OBJ/Module.symvers; then
                SYMVERFILE=$LINUX_OBJ/Module.symvers
        elif test -f $LINUX_OBJ/Modules.symvers; then
                SYMVERFILE=$LINUX_OBJ/Modules.symvers
        else
                AC_MSG_ERROR([Failed to find symbols files in $LINUX_OBJ])
        fi
	AC_MSG_RESULT([$SYMVERFILE])

        [AC_MSG_CHECKING([for symbol $1 exported])
        grep -q -E '[[[:space:]]]$1[[[:space:]]]' $SYMVERFILE 2>/dev/null
        rc=$?
        if test $rc -eq 0; then
    	        AC_MSG_RESULT([yes])
    	        $2
        else
                AC_MSG_RESULT([no])
                $3
        fi
])

AC_DEFUN([COLIBRI_LINUX_CONFTEST],
[cat >conftest.c <<_ACEOF
$1
_ACEOF
])

AC_DEFUN([COLIBRI_LANG_PROGRAM],
[$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}])

AC_DEFUN([COLIBRI_LINUX_COMPILE_IFELSE],
[m4_ifvaln([$1], [COLIBRI_LINUX_CONFTEST([$1])])
rm -f config/conftest.o config/conftest.mod.c config/conftest.ko
AS_IF([AC_TRY_COMMAND([cp conftest.c config && cd config && make -d $2 ${LD:+"LD=$LD"} CC="$CC" LINUX_OBJ=$LINUX_OBJ]) >/dev/null && AC_TRY_COMMAND([$3])],
	[$4],
	[_AC_MSG_LOG_CONFTEST
m4_ifvaln([$5],[$5])])
rm -f config/conftest.o config/conftest.mod.c config/conftest.mod.o config/conftest.ko m4_ifval([$1], [config/conftest.c conftest.c])[]
])

AC_DEFUN([COLIBRI_LINUX_TRY_COMPILE],[
        COLIBRI_LINUX_COMPILE_IFELSE(
                [AC_LANG_SOURCE([COLIBRI_LANG_PROGRAM([[$1]], [[$2]])])],
                [modules],
                [test -s config/conftest.o],
                [$3], [$4])
])

AC_DEFUN([COLIBRI_LINUX_CONFIG],
[AC_MSG_CHECKING([if Linux was built with CONFIG_$1])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/autoconf.h>
],[
#ifndef CONFIG_$1
#error CONFIG_$1 not #defined
#endif
],[
        AC_MSG_RESULT([yes])
        $2
],[
        AC_MSG_RESULT([no])
        $3
])
])

AC_DEFUN([COLIBRI_LINUX_FILE_READV],
[AC_MSG_CHECKING([readv in fops])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
],[
        struct file_operations *fops = NULL;
        fops->readv = NULL;
],[
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_FILE_READV])
],[
        AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_FILE_AIO_READ],
[AC_MSG_CHECKING([->aio_read in fops])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
],[
        struct file_operations *fops = NULL;
        fops->aio_read = NULL;
],[
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_FILE_AIO_READ])
],[
	AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_INODE_BLKSIZE],
[AC_MSG_CHECKING([inode has i_blksize field])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
],[
	struct inode i;
	i.i_blksize = 0;
],[
	AC_MSG_RESULT([yes])
	AC_DEFINE([HAVE_INODE_BLKSIZE])
],[
	AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_WRITE_BEGIN_END],
[AC_MSG_CHECKING([if kernel has .write_begin/end])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
#ifdef HAVE_MMTYPES_H
#include <linux/mm_types.h>
#endif
#include <linux/pagemap.h>
],[
        struct address_space_operations aops;
        struct page *page;

        aops.write_begin = NULL;
        aops.write_end = NULL;
        page = grab_cache_page_write_begin(NULL, 0, 0);
], [
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_WRITE_BEGIN_END])
],[
        AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_SENDFILE],
[
AC_MSG_CHECKING([if kernel has .sendfile])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
],[
        struct file_operations file;

        file.sendfile = NULL;
], [
        AC_MSG_RESULT([yes])
        AC_DEFINE([HAVE_SENDFILE])
],[
        AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_INIT_CONFIG], [
rm -fr config > /dev/null 2>&1
mkdir config
cat >config/Makefile <<_ACEOF
obj-m := conftest.o

modules:
	\$(MAKE) -C \$(LINUX_OBJ) M=\`pwd\` modules
_ACEOF
])

# Checks for programs.
AM_PROG_CC_C_O
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_AWK
AC_PROG_LIBTOOL
AM_PROG_CC_C_O

AC_PATH_PROG([NM_B], [nm], [null])
if test "x$NM_B" = "xnull"; then
        AC_MSG_ERROR([Cannot find nm program])
fi

SRCDIR=$(cd $srcdir && echo $PWD)
if ! test -d $SRCDIR; then
	AC_MSG_ERROR([Cannot figure out full path for srcdir!])
fi
AC_SUBST([SRCDIR])

AC_MSG_CHECKING([whether target platform is 64bit one])
AC_COMPUTE_INT(n_bits, [sizeof(long) * 8])
if test $n_bits -lt 64; then
	AC_MSG_RESULT([no])
	AC_MSG_ERROR([Only 64bit platforms are supported!])
fi
AC_MSG_RESULT([yes])

# Checks for libraries.
OLD_LIBS=$LIBS
AC_SEARCH_LIBS([xdr_domainname], [c nsl], [], [AC_MSG_ERROR([xdr_domainname cannot be found!])])
NSL_LIBS=$LIBS
LIBS=$OLD_LIBS
AC_SUBST([NSL_LIBS])

OLD_LIBS=$LIBS
AC_SEARCH_LIBS([pthread_create], [c pthread], [], [AC_MSG_ERROR([pthread_create cannot be found!])])
PTHREAD_LIBS=$LIBS
LIBS=$OLD_LIBS
AC_SUBST([PTHREAD_LIBS])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([pthread.h], [], [AC_MSG_ERROR([pthread.h cannot be found!])])
AC_CHECK_HEADERS([netinet/in.h], [AC_DEFINE([HAVE_NETINET_IN_H])])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

AC_CHECK_DECL([mallinfo], [AC_DEFINE([HAVE_MALLINFO])], [], [[#include <malloc.h>]])
AC_CHECK_DECL([malloc_size], [AC_DEFINE([HAVE_MALLOC_SIZE])], [], [[#include <malloc/malloc.h>]])
AC_CHECK_DECL([backtrace], [AC_DEFINE([HAVE_BACKTRACE])], [], [[#include <execinfo.h>]])
AC_CHECK_TYPES([pthread_barrier_t], [AC_DEFINE([HAVE_PTHREAD_BARRIER_T])], [], [[#include <pthread.h>]])

# Checks for library functions.
AC_FUNC_MALLOC

# Configure options
AC_ARG_ENABLE([sync_atomic],
        [AC_HELP_STRING([--enable-sync_atomic], [enable gcc built-in __sync_atomic functions])], [],
        [enable_sync_atomic=no]
)

AC_ARG_ENABLE([debug],
        [AC_HELP_STRING([--enable-debug], [enable debug information and logs])], [],
        [enable_debug=no]
)

AC_ARG_ENABLE([finject],
        [AC_HELP_STRING([--disable-finject], [disable fault injection support])], [],
        [enable_finject=yes]
)

AC_ARG_ENABLE([immediate-trace],
        [AC_HELP_STRING([--disable-immediate-trace], [disable immediate tracing])], [],
        [enable_immediate_trace=yes]
)

AC_ARG_ENABLE([mcheck],
        [AC_HELP_STRING([--enable-mcheck], [enable glibc malloc check])], [],
        [enable_mcheck=no]
)

AC_ARG_ENABLE([rpm],
        [AC_HELP_STRING([--enable-rpm], [enable rpm build mode])], [],
        [enable_rpm=no]
)

AC_MSG_CHECKING([for operating system])
AC_MSG_RESULT([$host_os])
case $host_os in
        linux*)
                ;;
        *)
                AC_MSG_ERROR([Unsupported platform detected!])
                ;;
esac

AC_MSG_CHECKING([for linux objects dir])
AC_ARG_WITH([linux-obj],
        [AC_HELP_STRING([--with-linux-obj=path],
                        [set path to Linux objects dir (default=/lib/modules/$(uname -r)/build])],
        [LINUX_OBJ=$with_linux_obj],
        [LINUX_OBJ=/lib/modules/`uname -r`/build]
)

AC_MSG_RESULT([$LINUX_OBJ])
AC_SUBST([LINUX_OBJ])

LINUX_MOD=$(dirname $LINUX_OBJ)
AC_SUBST([LINUX_MOD])

# gccxml is required by xcode/gccxml2xcode script
AC_CHECK_PROG(GCCXML, gccxml, gccxml)
if test -z "${GCCXML}"; then
        AC_MSG_ERROR(gccxml not found: it can be installed from \
gccxml package on Red Hat based systems)
fi

# c++ compiler is required by gccxml
AC_CHECK_PROG(CXX, g++, g++)
if test -z "${CXX}"; then
        AC_MSG_ERROR(g++ not found: it can be installed from \
gcc-c++ package on Red Hat based systems)
else
        AC_PROG_CXX()
fi

# perl and several perl-module are required by xcode/gccxml2xcode script
AX_PROG_PERL_MODULES(XML::LibXML, , AC_MSG_ERROR(No XML::LibXML Perl module \
found: it can be installed from perl-XML-LibXML package on Red Hat based systems))

AX_PROG_PERL_MODULES(List::MoreUtils, , AC_MSG_ERROR(No List::MoreUtils Perl \
module found: it can be installed from perl-List-MoreUtils package on Red Hat \
based systems))

# following two perl modules are required for scripts/check-include-guards
# script
AX_PROG_PERL_MODULES(File::Find::Rule, , AC_MSG_ERROR(No File::Find::Rule Perl \
module found: it can be installed from perl-File-Find-Rule package on Red Hat \
based systems))

AX_PROG_PERL_MODULES(IO::All, , AC_MSG_ERROR(No IO::All Perl \
module found: it can be installed from perl-IO-All package on Red Hat \
based systems))

AC_ARG_ENABLE([coverage],
	[AC_HELP_STRING([--enable-coverage], [build for test coverage analysis])],
	[case "${enableval}" in
		yes) ENABLE_COVERAGE=1 ;;
		no) ENABLE_COVERAGE=0 ;;
		*) AC_MSG_ERROR([Invalid value ${enableval} for --enable-coverage]) ;;
	esac],
	[ENABLE_COVERAGE=0])

AC_SUBST([ENABLE_COVERAGE])

if test "x[$]ENABLE_COVERAGE" = "x1"
then
	# Remove -O<digit> flag from $CFLAGS
	CFLAGS="`echo $CFLAGS | sed 's/-O[[0-9]]//g'` --coverage"
	LDFLAGS="-lgcov"
	K_ENABLE_COVERAGE=y
else
	K_ENABLE_COVERAGE=n
fi

AC_SUBST([K_ENABLE_COVERAGE])

# Prepare linux configuration checks facility. Should be done before
# any *_LINUX_* checks.
COLIBRI_LINUX_INIT_CONFIG

COLIBRI_LINUX_CHECK_SYMBOL([journal_abort],
        [AC_DEFINE([HAVE_JOURNAL_ABORT])])

COLIBRI_LINUX_INODE_BLKSIZE
COLIBRI_LINUX_FILE_READV
COLIBRI_LINUX_FILE_AIO_READ
COLIBRI_LINUX_WRITE_BEGIN_END
COLIBRI_LINUX_SENDFILE

AC_MSG_CHECKING([for LNet header dir])
AC_ARG_WITH([lustre],
        [AC_HELP_STRING([--with-lustre=path], [set path to lustre sources])],
        [LUSTRE=$with_lustre],
        [LUSTRE=/usr/src/`ls /usr/src | grep lustre | tail -1`]
)

if ! test -d $LUSTRE/lnet/include; then
	AC_MSG_ERROR([lnet/include not found in $LUSTRE!])
fi
AC_MSG_RESULT([$LUSTRE])

# Disabling check for lustre config and Module.symvers until devel RPM is ready
#AC_MSG_CHECKING([for Lustre config.h])
#if ! test -e $LUSTRE/config.h; then
#	AC_MSG_ERROR([$LUSTRE/config.h not found! Did you configure lustre?])
#fi
#AC_MSG_RESULT([$LUSTRE/config.h])
#AC_MSG_CHECKING([for Lustre symbols file])
#if ! test -e $LUSTRE/Module.symvers; then
#	AC_MSG_ERROR([$LUSTRE/Module.symvers not found! Did you build lustre?])
#fi
#AC_MSG_RESULT([$LUSTRE/Module.symvers])
AC_SUBST([LUSTRE])

build_lib() {
       NAME=$1
       SRC=$2
       LIBS=$3
       SUBP=$4
       OPTS=$5
       if ! test -e $LIBS; then
               AC_MSG_RESULT([building $SRC])
               oldcwd=$(pwd)
               cd $SRC && $SUBP/configure $OPTS && make
               res=$?
               cd $oldcwd
               if ! test $res -eq 0 ; then
                       AC_MSG_ERROR([An error occured while building lib$NAME!])
               fi
               if ! test -e $LIBS; then
                       AC_MSG_ERROR([$LIBS not found! Build failed?])
               fi
       else
               AC_MSG_RESULT([$LIBS])
       fi
}

OLD_LIBS=$LIBS
OLD_CFLAGS=$CFLAGS
LIBS=""
CFLAGS=""

# Check for galois
if test x$enable_rpm = xyes; then

OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS([galois_create_mult_tables], [galois], [
        AC_CHECK_HEADERS([galois/galois.h], [],
        [
                AC_MSG_ERROR([galois/galois.h is not found. Try install galois from $SRCDIR/../galois or from rpm])
        ],[])
], [
        AC_MSG_ERROR([galois_create_mult_tables cannot be found! Try install galois package from $SRCDIR/../galois or from rpm])
])
GALOIS_SRC=""
GALOIS_LIBS=$LIBS
LIBS=$OLD_LIBS

galois_locations="/usr /usr/local /opt /opt/local"
AC_MSG_CHECKING([galois symvers])
GALOIS_SYMVERS=""
for p in $galois_locations; do
        symvers="$p/share/galois/symvers/Module.symvers"
        if test -f $symvers; then
                GALOIS_SYMVERS=$symvers
                break
        fi
done

if ! test x$GALOIS_SYMVERS = "x"; then
        AC_MSG_RESULT([$GALOIS_SYMVERS])
        AC_SUBST([GALOIS_SYMVERS])
else
        AC_MSG_ERROR([Galois Module.symvers is not found in the following locations: ${galois_locations}])
fi

else

GALOIS_SRC=$SRCDIR/../galois
GALOIS_LIBS="$GALOIS_SRC/src/libgalois.la"
AC_MSG_CHECKING([for galois libs])
build_lib galois $GALOIS_SRC $GALOIS_LIBS "." "--with-linux-obj=$LINUX_OBJ"

AC_MSG_CHECKING([galois symvers])
GALOIS_SYMVERS="$GALOIS_SRC/src/linux_kernel/Module.symvers"
if test -f $GALOIS_SYMVERS; then
        AC_MSG_RESULT([$GALOIS_SYMVERS])
        AC_SUBST([GALOIS_SYMVERS])
else
        AC_MSG_ERROR([Can't find ${GALOIS_SYMVERS}! Make sure that galois is built correctly.])
fi
fi

# Check for yaml
if test x$enable_rpm = xyes; then

OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS([yaml_parser_initialize], [yaml], [], [AC_MSG_ERROR([yaml_parser_initialize cannot be found! Try install yaml package from $SRCDIR/../yaml or from rpm])])
YAML_LIBS=$LIBS
LIBS=$OLD_LIBS

else

YAML_SRC=$SRCDIR/../yaml
YAML_LIBS="$YAML_SRC/src/libyaml.la"
AC_MSG_CHECKING([for yaml libs])
build_lib yaml $YAML_SRC $YAML_LIBS "."

fi

# Check for CUnit
if test x$enable_rpm = xyes; then

OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS([CU_add_suite], [cunit],
[
        AC_CHECK_HEADERS([CUnit/Basic.h], [],
        [
                AC_MSG_ERROR([CUnit/Basic.h is not found. Try install CUnit from $SRCDIR/../cunit or CUnit-devel rpm package])
        ],[])
], [
        AC_MSG_ERROR([CU_add_suite cannot be found! Try install CUnit package from $SRCDIR/../cunit or from rpm])
], [-lncurses])
CUNIT_LIBS=$LIBS
LIBS=$OLD_LIBS

else

CUNIT_SRC=$SRCDIR/../cunit
CUNIT_LIBS="$CUNIT_SRC/CUnit/Sources/libcunit.la"
CUNIT_HEADERS="$CUNIT_SRC/CUnit/Headers"
AC_MSG_CHECKING([for cunit libs])
build_lib cunit $CUNIT_SRC $CUNIT_LIBS "." "--enable-curses"

fi

# Check for berkley db
if test x$enable_rpm = xyes; then

OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS([db_env_create], [db], [
        AC_CHECK_HEADERS([db.h], [],
        [
                AC_MSG_ERROR([db.h is not found. Try install db4-devel package])
        ],[])
], [
        AC_MSG_ERROR([db_env_create cannot be found! Try install db4-devel package])
])
DB_LIBS=$LIBS
DB_INCLUDES=""
else

DB_SRC="$SRCDIR/../db4/build_unix"
DB_INCLUDES="-I$DB_SRC/build_unix -I$DB_SRC"
DB_LIBS="$DB_SRC/libdb.a"
AC_MSG_CHECKING([for db libs])
build_lib db $DB_SRC $DB_LIBS "../dist" "--with-pic"

fi

LIBS=$OLD_LIBS
CFLAGS=$OLD_CFLAGS

AC_SUBST([DB_SRC])
AC_SUBST([DB_LIBS])
AC_SUBST([DB_INCLUDES])
AC_SUBST([GALOIS_SRC])
AC_SUBST([GALOIS_LIBS])
AC_SUBST([YAML_LIBS])
AC_SUBST([CUNIT_LIBS])

# Check for libaio
OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS([io_getevents], [c aio], [], [AC_MSG_ERROR([io_getevents cannot be found! Try install libaio-devel.])])
AIO_LIBS=$LIBS
AC_SUBST([AIO_LIBS])
LIBS=$OLD_LIBS

if test x$enable_sync_atomic = xyes; then
	AC_DEFINE([ENABLE_SYNC_ATOMIC])
fi

if test x$enable_debug = xyes; then
        # If debug options specified we are free to strip all the default
        # optimization and debug flags.
        CFLAGS="$(echo $CFLAGS | sed 's/-g//' | sed 's/-O2//') -g -O0"
        KCFLAGS="$(echo $KCFLAGS | sed 's/-g//' | sed 's/-O2//') -g -O0"
        AC_DEFINE([ENABLE_DEBUG])
fi

if test x$enable_finject = xyes; then
        AC_DEFINE([ENABLE_FAULT_INJECTION])
fi

if test x$enable_immediate_trace = xyes; then
        AC_DEFINE([ENABLE_IMMEDIATE_TRACE])
fi

if test x$enable_mcheck = xyes; then
	LDFLAGS="$LDFLAGS -lmcheck"
fi

MAKEFLAGS=""
if test x$enable_silent_rules = xyes; then
	MAKEFLAGS="--no-print-directory"
fi
AC_SUBST([MAKEFLAGS])


# Check if gcc option is supported in current gcc version.
AC_DEFUN([COLIBRI_COMPILER_OPTION_CHECK],
[AC_MSG_CHECKING([whether compiler $CC supports $1 option])
OLD_CFLAGS="$CFLAGS"
CFLAGS="$OLD_CFLAGS $1"
AC_COMPILE_IFELSE([COLIBRI_LANG_PROGRAM()],
        [AC_MSG_RESULT([yes]);
         KCFLAGS="$KCFLAGS $1"],
        [AC_MSG_RESULT([no]);
	 KCFLAGS="$KCFLAGS"]
)]
CFLAGS="$OLD_CFLAGS")

#
# _GNU_SOURCE for asprintf(3), open(2) O_DIRECT flag
#
# -Wno-attributes is required to suppress warnings about unrecognized
# __attribute__, such as __attribute__((gccxml(...))). It's important because we
# use -Werror, which turns warnings into errors, but we still need to be able to
# use gccxml attributes.
#
if test x$enable_rpm = xyes; then

GALOIS_INCLUDES="-I/usr/local/include -I/usr/include"
CFLAGS="-Wall -Werror -Wno-attributes -D_REENTRANT -D_GNU_SOURCE \
        -fno-strict-aliasing -iquote . -iquote $SRCDIR $CFLAGS"

else

GALOIS_INCLUDES="-I$GALOIS_SRC/include"
CFLAGS="-Wall -Werror -Wno-attributes -D_REENTRANT -D_GNU_SOURCE \
        -fno-strict-aliasing $DB_INCLUDES $GALOIS_INCLUDES -I$YAML_SRC/include \
        -I$CUNIT_HEADERS -iquote . -iquote $SRCDIR $CFLAGS"
fi

#
# Galois header may be in one of the following standard loctions.
# Kernel needs to know about.
#
KCFLAGS="-Wall -Werror -Wno-attributes $KCFLAGS $GALOIS_INCLUDES"
COLIBRI_COMPILER_OPTION_CHECK([-Wno-unused-but-set-variable])

LDFLAGS="-rdynamic $LDFLAGS"

CFLAGS="$CFLAGS -include config.h"
KCFLAGS="$KCFLAGS -include config.h"

# trim spaces at the end of flags
CFLAGS=$(echo $CFLAGS | sed 's/[[[:blank:]]]*$//')
KCFLAGS=$(echo $KCFLAGS | sed 's/[[[:blank:]]]*$//')
LDFLAGS=$(echo $LDFLAGS | sed 's/[[[:blank:]]]*$//')
AC_SUBST([KCFLAGS])

# Please keep this list sorted (always appending new items at the end
# results in needless merge conflicts)
AC_CONFIG_FILES([
                 Makefile
                 addb/Makefile
		 addb/addbff/Makefile
		 addb/ut/Makefile
                 balloc/Makefile
                 balloc/ut/Makefile
                 build_kernel_modules/Makefile
                 capa/Makefile
                 capa/ut/Makefile
                 c2t1fs/linux_kernel/Makefile
                 c2t1fs/linux_kernel/st/Makefile
                 c2t1fs/linux_kernel/st/common.sh
                 c2t1fs/linux_kernel/ut/Makefile
                 cfg/Makefile
		 cm/Makefile
		 cm/ut/Makefile
                 cob/Makefile
                 cob/ut/Makefile
                 colibri/Makefile
                 colibri/ut/Makefile
		 console/Makefile
                 console/bin/Makefile
                 console/ut/Makefile
                 console/st/Makefile
                 db/Makefile
                 db/ut/Makefile
                 desim/Makefile
                 desim/ut/Makefile
                 doc/Makefile
                 dtm/Makefile
                 fid/Makefile
                 fol/Makefile
                 fol/ut/Makefile
                 fop/Makefile
                 fop/ut/Makefile
                 fop/ut/long_lock/Makefile
                 ioservice/Makefile
                 ioservice/ut/Makefile
                 layout/Makefile
                 layout/ut/Makefile
                 lib/Makefile
                 lib/user_space/Makefile
                 lib/ut/Makefile
                 m4/Makefile
                 man/Makefile
                 mds/Makefile
                 mw/Makefile
                 net/Makefile
		 net/bulk_emulation/Makefile
		 net/bulk_emulation/st/Makefile
		 net/bulk_emulation/ut/Makefile
		 net/lnet/Makefile
		 net/lnet/st/Makefile
		 net/lnet/st/linux_kernel/Makefile
		 net/lnet/ut/Makefile
		 net/test/Makefile
		 net/test/linux_kernel/Makefile
		 net/test/test-user.sh
		 net/test/ntc.sh
		 net/test/ntn.sh
		 net/test/user_space/Makefile
		 net/test/ut/Makefile
		 net/ut/Makefile
                 nrs/Makefile
                 patches/Makefile
                 pool/Makefile
                 reqh/Makefile
		 reqh/ut/Makefile
                 rpc/Makefile
                 rpc/ut/Makefile
                 rpc/it/Makefile
		 rpc/it/linux_kernel/Makefile
		 scripts/Makefile
                 sm/Makefile
                 sm/ut/Makefile
                 sns/Makefile
                 sns/repair/Makefile
                 sns/repair/ut/Makefile
                 sns/ut/Makefile
                 stob/Makefile
                 stob/ut/Makefile
                 udb/Makefile
                 udb/ut/Makefile
                 ut/Makefile
                 ut/linux_kernel/Makefile
                 utils/Makefile
                 utils/linux_kernel/Makefile
                 utils/ploss/Makefile
                 utils/ut.sh
                 utils/linux_kernel/ut.sh
                 xcode/Makefile
                 xcode/ff2c/Makefile
                 xcode/ut/Makefile
		 yaml2db/Makefile
		 yaml2db/ut/Makefile
		 yaml2db/st/Makefile
		 colibri.spec
                ])
AC_OUTPUT

#
# Set up execute bit for UT scripts.
#
chmod +x net/test/ntc.sh
chmod +x net/test/ntn.sh
chmod +x net/test/test-user.sh
chmod +x utils/ut.sh
chmod +x utils/linux_kernel/ut.sh

echo ""
echo "GALOIS_LIBS :  \"$GALOIS_LIBS\""
echo "CUNIT_LIBS  :  \"$CUNIT_LIBS\""
echo "YAML_LIBS   :  \"$YAML_LIBS\""
echo "DB_LIBS     :  \"$DB_LIBS\""
echo "AIO_LIBS    :  \"$AIO_LIBS\""
echo ""
echo "CFLAGS      :  \"$CFLAGS\""
echo "KCFLAGS     :  \"$KCFLAGS\""
echo "LDFLAGS     :  \"$LDFLAGS\""
echo ""

echo "Linux Obj   :  \"$LINUX_OBJ\""
echo "Lustre      :  \"$LUSTRE\""
echo ""

echo "Type 'make' to build Colibri"
