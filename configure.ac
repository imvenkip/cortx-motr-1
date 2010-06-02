AC_PREREQ([2.59])
AC_INIT([colibri], [0.1], [colibri@clusterstor.com])
AC_CONFIG_SRCDIR([colibri/init.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AH_TEMPLATE([PACKAGE], [Package name.])
AH_TEMPLATE([VERSION], [Version of the package.])
AH_TEMPLATE([ENABLE_DEBUG], [Enable debug info.])
AH_TEMPLATE([ENABLE_GSSRPC], [Enable gssrpc.])
AH_TEMPLATE([HAVE_JOURNAL_ABORT], [Have journal_abort() function])
AH_TEMPLATE([HAVE_FILE_READV], [Have fops->readv function])
AH_TEMPLATE([HAVE_FILE_AIO_READ], [Have fops->aio_read function])
AH_TEMPLATE([HAVE_INODE_BLKSIZE], [Have inode->i_blksize field])
AH_TEMPLATE([HAVE_WRITE_BEGIN_END], [Have .begin_write/end_write])
AH_TEMPLATE([HAVE_SENDFILE], [Have .sendfile])
AH_TEMPLATE([HAVE_MALLINFO], [Have mallinfo() function])
AH_TEMPLATE([HAVE_MALLOC_SIZE], [Have malloc_size() function])
AH_TEMPLATE([HAVE_BACKTRACE], [Have backtrace(3) function])
AH_TEMPLATE([LINUX], [Linux operating system])
AH_TEMPLATE([DARWIN], [Darwin operating system])

COLIBRI_MAJOR_VERSION=0
COLIBRI_MINOR_VERSION=1

PACKAGE=colibri
VERSION=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION

AM_INIT_AUTOMAKE([$PACKAGE], [$VERSION])

LT_RELEASE=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION
AC_SUBST([LT_RELEASE])

AC_DEFUN([COLIBRI_LINUX_CHECK_SYMBOL],
        AC_MSG_CHECKING([for symbols file])
        if grep -q Modules.symvers $LINUX/scripts/Makefile.modpost ; then
                SYMVERFILE=Modules.symvers
        else
                SYMVERFILE=Module.symvers
	fi
	AC_MSG_RESULT([$LINUX/$SYMVERFILE])

        [AC_MSG_CHECKING([for symbol $1 exported])
        grep -q -E '[[[:space:]]]$1[[[:space:]]]' $LINUX/$SYMVERFILE 2>/dev/null
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

USER_CFLAGS=$CFLAGS
USER_KCFAGS=$KCFLAGS

# Checks for programs.
AC_PROG_CC
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
AC_ARG_ENABLE([debug],
        [AC_HELP_STRING([--disable-debug], [disable debug information and logs])], [],
        [enable_debug=yes]
)

AC_ARG_ENABLE([gssrpc],
        [AC_HELP_STRING([--enable-gssrpc], [enable gssrpc support])], [],
        [enable_gssrpc=no]
)

AC_ARG_ENABLE([db-o_direct],
        [AC_HELP_STRING([--enable-db-o_direct], [enable db direct IO support])], [],
        [enable_db_o_direct=no]
)

case $host_os in
        darwin*) OS="darwin" ;;
        linux*) OS="linux" ;;
esac
AC_SUBST([OS])

if test x$OS = xlinux; then
        AC_DEFINE([LINUX])
fi
if test x$OS = xdarwin; then
        AC_DEFINE([DARWIN])
fi

AM_CONDITIONAL([LINUX], [test x$OS = xlinux])
AM_CONDITIONAL([DARWIN], [test x$OS = xdarwin])

if test x$OS = xlinux; then
        AC_MSG_CHECKING([for linux sources dir])
        AC_ARG_WITH([linux],
                [AC_HELP_STRING([--with-linux=path], [set path to linux sources])],
                [LINUX=$with_linux],
                [LINUX=/lib/modules/`uname -r`/build]
        )

        AC_MSG_RESULT([$LINUX])
        AC_SUBST([LINUX])

        AC_MSG_CHECKING([for linux objects dir])
        AC_ARG_WITH([linux-obj],
                [AC_HELP_STRING([--with-linux-obj=path], [set path to linux objects dir])],
                [LINUX_OBJ=$with_linux_obj],
                [LINUX_OBJ=$LINUX]
        )

        AC_MSG_RESULT([$LINUX_OBJ])
        AC_SUBST([LINUX_OBJ])

        LINUX_MOD="/lib/modules/`uname -r`"
        AC_SUBST([LINUX_MOD])

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
fi

AC_ARG_WITH([db],
        [AC_HELP_STRING([--with-db=path], [path where db was built])])

OLD_LIBS=$LIBS
OLD_CFLAGS=$CFLAGS
LIBS=""
CFLAGS=""

AC_MSG_CHECKING([for db libs])
if test x$with_db != x; then
	DBSRC="$with_db"
        DBLIBS="$(ls $DBSRC/build_unix/libdb*.la)"
	if ! test -e $DBLIBS; then
		AC_MSG_ERROR([$DBLIBS not found! Did you build db?])
	fi
        AC_MSG_RESULT([$DBLIBS])
else
	DBSRC="$SRCDIR/../db4"
        DBLIBS="$(ls $DBSRC/build_unix/libdb*.la)"
	if ! test -e $DBLIBS; then
		oldcwd=$(pwd)
		if test x$enable_db_o_direct = xyes; then
		        o_direct_options="--enable-o_direct"
		else
		        o_direct_options=""
		fi
                AC_MSG_RESULT([building $DBSRC])

                # We need -fPIC to link libdb.a (static library) into so libraries, which should
                # have position intependent code as nobody case foresee in what addresses inside
                # calling process it will be mapped
                AC_MSG_NOTICE([cd $DBSRC/build_unix && ../dist/configure $o_direct_options && make])
		cd $DBSRC/build_unix && ../dist/configure CFLAGS=-fPIC $o_direct_options && make
		res=$?
		cd $oldcwd
		if ! test $res -eq 0 ; then
			AC_MSG_ERROR([An error occured while building db!])
		fi
		if ! test -e $DBLIBS; then
			AC_MSG_ERROR([$DBLIBS not found! Build failed?])
	        fi
	else
                DBLIBS="$(ls $DBSRC/build_unix/libdb*.la)"
                AC_MSG_RESULT([$DBLIBS])
	fi
fi

# Check if o_direct patch applied
if test x$OS = xlinux -a x$enable_db_o_direct = xyes; then
        AC_MSG_CHECKING([for direct I/O patch applied to db])
        $NM_B $DBLIBS | grep -q -E '[[[:digit:]]]*[[[:space:]]]T[[[:space:]]]__os_calloc_for_IO';
        rc=$?
        if ! test $rc -eq 0; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([No direct I/O patch applied to db at $DBSRC found!])
        else
                AC_MSG_RESULT([yes])
        fi
fi

AC_SUBST([DBSRC])
AC_SUBST([DBLIBS])

LIBS=$OLD_LIBS
CFLAGS=$OLD_CFLAGS

if test x$enable_gssrpc = xyes; then
        AC_CHECK_LIB([gssrpc], [xdr_int], [
            AC_CHECK_HEADERS([gssrpc/rpc.h gssrpc/pmap_clnt.h], [AC_DEFINE([ENABLE_GSSRPC])],
                [AC_MSG_NOTICE([gssrpc/pmap_clnt.h gssrpc/rpc.h is not found. Gssrpc is disabled!])],
                [[
                    #ifdef HAVE_GSSRPC_RPC_H
                    #include <gssrpc/rpc.h>
                    #endif
                ]])
            ],
            [AC_MSG_NOTICE([libgssrpc is not found. Gssrpc is disabled!])]
        )
fi

if test x$OS = xlinux; then
        OLD_LIBS=$LIBS
        LIBS=""
        AC_SEARCH_LIBS([io_getevents], [c aio], [], [AC_MSG_ERROR([io_getevents cannot be found!])])
        AIO_LIBS=$LIBS
        AC_SUBST([AIO_LIBS])
        LIBS=$OLD_LIBS
fi

CFLAGS=""
KCFLAGS=""
if test x$enable_debug = xyes; then
        echo $USER_CFLAGS | grep -q "\-O" || CFLAGS="-O2"
        echo $USER_CFLAGS | grep -q "\-g" || CFLAGS="$CFLAGS -g"
        echo $USER_KCFLAGS | grep -q "\-O" || KCFLAGS="-O2"
        echo $USER_KCFLAGS | grep -q "\-g" || KCFLAGS="$KCFLAGS -g"
        AC_DEFINE([ENABLE_DEBUG])
else
        echo $USER_CFLAGS | grep -q "\-O" || CFLAGS="-O2"
        echo $USER_KCFLAGS | grep -q "\-O" || KCFLAGS="-O2"
fi

if test x$OS = xdarwin; then
        CFLAGS="$CFLAGS -fnested-functions"
fi

# trim spaces at the end of flags
CFLAGS=$(echo $CFLAGS | sed 's/[[:blank:]]*//')
KCFLAGS=$(echo $KCFLAGS | sed 's/[[:blank:]]*//')

CFLAGS="-Wall -Werror -D_REENTRANT -fno-strict-aliasing $CFLAGS $USER_CFLAGS"
KCFLAGS="-Wall -Werror $KCFLAGS $USER_KCFLAGS"
LDFLAGS="-rdynamic"

# trim spaces at the end of flags
CFLAGS=$(echo $CFLAGS | sed 's/[[:blank:]]*$//')
KCFLAGS=$(echo $KCFLAGS | sed 's/[[:blank:]]*$//')
AC_SUBST([KCFLAGS])

AC_CONFIG_FILES([
		 Makefile
		 m4/Makefile
                 doc/Makefile
                 include/Makefile
                 include/colibri/Makefile
                 man/Makefile
                 colibri/Makefile
                 lib/Makefile
                 lib/ut/Makefile
                 dtm/Makefile
                 addb/Makefile
                 fol/Makefile
                 fop/Makefile
                 fop/ut/Makefile
                 rpc/Makefile
                 rpc/ut/Makefile
                 net/Makefile
                 net/usunrpc/Makefile
                 net/ksunrpc/Makefile
                 net/ut/Makefile
                 nrs/Makefile
                 sns/Makefile
                 reqh/Makefile
                 sm/Makefile
                 mds/Makefile
                 mw/Makefile
                 c2t1fs/Makefile
                 build_kernel_modules/Makefile
                 patches/Makefile
                 stob/Makefile
                 stob/ut/Makefile
                 utils/Makefile
                 utils/ploss/Makefile
                ])
AC_OUTPUT

echo ""
echo "CFLAGS :  \"$CFLAGS\""
echo "KCFLAGS:  \"$KCFLAGS\""
echo "LDFLAGS:  \"$LDFLAGS\""
echo ""

if test x$OS = xlinux; then
echo "Linux  :  \"$LINUX\""
fi
echo "Db     :  \"$DBSRC\""
echo ""

echo "Type 'make' to build Colibri"
