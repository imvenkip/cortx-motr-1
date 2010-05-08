AC_PREREQ(2.59)
AC_INIT(colibri, 0.1, colibri-support@clusterstor.com)
AC_CONFIG_SRCDIR([c2t1fs/c2t1fs.c])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AH_TEMPLATE([PACKAGE], [Package name.])
AH_TEMPLATE([VERSION], [Version of the package.])
AH_TEMPLATE([ENABLE_DEBUG], [Enable debug info.])
AH_TEMPLATE([ENABLE_GSSRPC], [Enable gssrpc.])
AH_TEMPLATE([HAVE_JOURNAL_ABORT], [Have journal_abort() function])
AH_TEMPLATE([HAVE_FILE_READV], [Have fops->readv function])
AH_TEMPLATE([HAVE_FILE_WRITEV], [Have fops->writev function])
AH_TEMPLATE([HAVE_INODE_BLKSIZE], [Have inode->i_blksize field])
AH_TEMPLATE([HAVE_WRITE_BEGIN_END], [Have .begin_write/end_write])

COLIBRI_MAJOR_VERSION=0
COLIBRI_MINOR_VERSION=1

PACKAGE=colibri
VERSION=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

LT_RELEASE=$COLIBRI_MAJOR_VERSION.$COLIBRI_MINOR_VERSION
AC_SUBST(LT_RELEASE)

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
        AC_MSG_ERROR([Can't find nm program])
fi

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

m4_define([COLIBRI_LANG_PROGRAM],
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
[m4_ifvaln([$1], [COLIBRI_LINUX_CONFTEST([$1])])dnl
rm -f config/conftest.o config/conftest.mod.c config/conftest.ko
AS_IF([AC_TRY_COMMAND(cp conftest.c config && cd config && make -d [$2] ${LD:+"LD=$LD"} CC="$CC" LINUX_OBJ=$LINUX_OBJ) >/dev/null && AC_TRY_COMMAND([$3])],
	[$4],
	[_AC_MSG_LOG_CONFTEST
m4_ifvaln([$5],[$5])dnl])dnl
rm -f config/conftest.o config/conftest.mod.c config/conftest.mod.o config/conftest.ko m4_ifval([$1], [config/conftest.c conftest.c])[]dnl
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
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_FILE_READV, 1,
                [use fops->readv])
],[
        AC_MSG_RESULT(no)
])
])

AC_DEFUN([COLIBRI_LINUX_FILE_WRITEV],
[AC_MSG_CHECKING([writev in fops])
COLIBRI_LINUX_TRY_COMPILE([
#include <linux/fs.h>
],[
        struct file_operations *fops = NULL;
        fops->writev = NULL;
],[
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_FILE_WRITEV, 1,
                [use fops->writev])
],[
	AC_MSG_RESULT(no)
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
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_INODE_BLKSIZE, 1,
		[struct inode has i_blksize field])
],[
	AC_MSG_RESULT(no)
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
        AC_DEFINE(HAVE_WRITE_BEGIN_END, 1,
                [kernel has .write_begin/end])
],[
        AC_MSG_RESULT([no])
])
])

AC_DEFUN([COLIBRI_LINUX_SENDFILE],
[AC_MSG_CHECKING([if kernel has .sendfile])
COLIBRI_LINUX_TRY_COMPILE([
        #include <linux/fs.h>
],[
        struct file_operations file;

        file.sendfile = NULL;
], [
        AC_MSG_RESULT([yes])
        AC_DEFINE(HAVE_SENDFILE, 1,
                [kernel has .sendfile])
],[
        AC_MSG_RESULT([no])
])
])

SRCDIR=$(cd $srcdir && echo $PWD)
if ! test -d $SRCDIR; then
	AC_MSG_ERROR(Cannot figure out full path for srcdir!)
fi
AC_SUBST(SRCDIR)

# Checks for libraries.
OLD_LIBS=$LIBS
AC_SEARCH_LIBS(xdr_domainname, c nsl, , [AC_MSG_ERROR(xdr_domainname cannot be found!)])
NSL_LIBS=$LIBS
LIBS=$OLD_LIBS
AC_SUBST(NSL_LIBS)

OLD_LIBS=$LIBS
AC_SEARCH_LIBS(pthread_create, c pthread, , [AC_MSG_ERROR(pthread_create cannot be found!)])
PTHREAD_LIBS=$LIBS
LIBS=$OLD_LIBS
AC_SUBST(PTHREAD_LIBS)

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h pthread.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC

# Configure options
AC_ARG_ENABLE(debug,
        AC_HELP_STRING([--disable-debug], [disable debug information and logs]), ,
        enable_debug=yes
)

AC_ARG_ENABLE(gssrpc,
        AC_HELP_STRING([--enable-gssrpc], [enable gssrpc support]), ,
        enable_gssrpc=no
)

AC_ARG_ENABLE(db4-dio,
        AC_HELP_STRING([--enable-db4-dio], [enable db4 direct IO support]), ,
        enable_db4_dio=no
)

case $host_os in
        darwin*) OS="darwin" ;;
        linux*) OS="linux" ;;
esac
AC_SUBST(OS)

if test x$OS = xlinux; then
        AC_MSG_CHECKING([for linux sources dir])
        AC_ARG_WITH(linux,
                AC_HELP_STRING([--with-linux=path], [set path to linux sources]), 
                [LINUX=$with_linux],
                [LINUX=/lib/modules/`uname -r`/build]
        )

        AC_MSG_RESULT([$LINUX])
        AC_SUBST(LINUX)

        AC_MSG_CHECKING([for linux objects dir])
        AC_ARG_WITH(linux-obj,
                AC_HELP_STRING([--with-linux-obj=path], [set path to linux objects dir]), 
                [LINUX_OBJ=$with_linux_obj],
                [LINUX_OBJ=$LINUX]
        )

        AC_MSG_RESULT([$LINUX_OBJ])
        AC_SUBST(LINUX_OBJ)
        
        LINUX_MOD="/lib/modules/`uname -r`"
        AC_SUBST(LINUX_MOD)

        # Example of checking for kernel symbol
        COLIBRI_LINUX_CHECK_SYMBOL([journal_abort], 
                [AC_DEFINE([HAVE_JOURNAL_ABORT], [], [Have journal_abort symbol])], [])

        COLIBRI_LINUX_INODE_BLKSIZE
        COLIBRI_LINUX_FILE_READV
        COLIBRI_LINUX_FILE_WRITEV
        COLIBRI_LINUX_WRITE_BEGIN_END
        COLIBRI_LINUX_SENDFILE
fi

if test x$enable_debug = xyes; then
        CFLAGS="-Wall -Werror -O0 -g"
        KCFLAGS="-Wall -Werror -O0 -g"
        AC_DEFINE(ENABLE_DEBUG)
else
	CFLAGS="-Wall -Werror -O2"
        KCFLAGS="-Wall -Werror -O2"
fi
AC_SUBST(KCFLAGS)

AC_ARG_WITH([db4], 
        AC_HELP_STRING([--with-db4=path], [path of where db4 was built]), ,
)

OLD_LIBS=$LIBS
OLD_CFLAGS=$CFLAGS
LIBS=""
CFLAGS=""

AC_MSG_CHECKING([for db4 libs])
if test x$with_db4 != x; then
	DB4SRC="$with_db4"
        DB4LIBS="$DB4SRC/build_unix/libdb.a"
	if ! test -e $DB4LIBS; then
		AC_MSG_ERROR($DB4LIBS not found! Did you build db4?)
	fi
        AC_MSG_RESULT([$DB4LIBS])
else
	DB4SRC="$SRCDIR/../db4"
        DB4LIBS="$DB4SRC/build_unix/libdb.a"
	if ! test -e $DB4LIBS; then
		oldcwd=$(pwd)
		if test x$enable_db4_dio = xyes; then
		        dio_options="--enable-o_direct"
		else
		        dio_options=""
		fi
                AC_MSG_RESULT([building $DB4SRC])
                
                # We need -fPIC to link libdb.a (static library) into so libraries, which should 
                # have position intependent code as nobody case foresee in what addresses inside
                # calling process it will be mapped
                AC_MSG_NOTICE([cd $DB4SRC/build_unix && ../dist/configure CFLAGS=-fPIC $dio_options && make])
		cd $DB4SRC/build_unix && ../dist/configure CFLAGS=-fPIC $dio_options && make
		res=$?
		cd $oldcwd
		if ! test $res -eq 0 ; then 
			AC_MSG_ERROR(An error occured while building db4!)
		fi
		if ! test -e $DB4LIBS; then
			AC_MSG_ERROR($DB4LIBS not found! Build failed?)
	        fi
	else
                DB4LIBS="$DB4SRC/build_unix/libdb.a"
                AC_MSG_RESULT([$DB4LIBS])
	fi
fi

# Check if DIO patch applied
if test x$OS = xlinux -a x$enable_db4_dio = xyes; then
        AC_MSG_CHECKING([for DIO patch applied to db4])
        $NM_B $DB4LIBS | grep -q -E '[[[:digit:]]]*[[[:space:]]]T[[[:space:]]]__os_calloc_for_IO';
        rc=$?
        if ! test $rc -eq 0; then
                AC_MSG_RESULT([no])
                AC_MSG_ERROR([No DIO patch applied to db4 at $DB4SRC])
        else
                AC_MSG_RESULT([yes])
        fi
fi

AC_SUBST(DB4SRC)
AC_SUBST(DB4LIBS)

LIBS=$OLD_LIBS
CFLAGS=$OLD_CFLAGS

if test x$enable_gssrpc = xyes; then
        AC_CHECK_LIB(gssrpc, xdr_int, [
            AC_CHECK_HEADERS([gssrpc/rpc.h gssrpc/pmap_clnt.h], [AC_DEFINE(ENABLE_GSSRPC)], 
                [AC_MSG_NOTICE(gssrpc/pmap_clnt.h gssrpc/rpc.h is not found. Gssrpc is disabled!)],
                [[
                    #ifdef HAVE_GSSRPC_RPC_H
                    #include <gssrpc/rpc.h>
                    #endif
                ]])
            ], 
            AC_MSG_NOTICE(libgssrpc is not found. Gssrpc is disabled!)
        )
fi

AC_CONFIG_FILES([
		 Makefile
		 m4/Makefile
                 doc/Makefile
                 include/Makefile
                 include/colibri/Makefile
                 man/Makefile
                 lib/Makefile
                 lib/ut/Makefile
                 dtm/Makefile
                 addb/Makefile
                 fol/Makefile
                 fop/Makefile
                 rpc/Makefile
                 net/Makefile
                 nrs/Makefile
                 sns/Makefile
                 sm/Makefile
                 mds/Makefile
                 c2t1fs/Makefile
                 patches/Makefile
                 utils/Makefile
                 utils/ploss/Makefile
                ])
AC_OUTPUT
