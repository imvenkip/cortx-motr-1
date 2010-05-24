AC_PREREQ(2.59)
AC_INIT(colibri, 0.1, colibri-support@clusterstor.com)
AC_CONFIG_SRCDIR([include/colibri/colibri.h])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

AH_TEMPLATE([PACKAGE], [Package name.])
AH_TEMPLATE([VERSION], [Version of the package.])
AH_TEMPLATE([ENABLE_DEBUG], [Enable debug info.])
AH_TEMPLATE([ENABLE_GSSRPC], [Enable gssrpc.])
AH_TEMPLATE([HAVE_JOURNAL_ABORT], [Have journal_abort() function])
AH_TEMPLATE([HAVE_MALLINFO], [Have mallinfo() function])
AH_TEMPLATE([HAVE_MALLOC_SIZE], [Have malloc_size() function])
AH_TEMPLATE([HAVE_BACKTRACE], [Have backtrace(3) function])

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

# New macros for checking various things
AC_DEFUN([COLIBRI_LINUX_CHECK_SYMBOL],
        AC_MSG_CHECKING([for symbols file])
        if grep -q Modules.symvers $KERNELDIR/scripts/Makefile.modpost ; then
                SYMVERFILE=Modules.symvers
        else
                SYMVERFILE=Module.symvers
	fi
	AC_MSG_RESULT([$KERNELDIR/$SYMVERFILE])
	
        [AC_MSG_CHECKING([for symbol $1 exported])
        grep -q -E '[[[:space:]]]$1[[[:space:]]]' $KERNELDIR/$SYMVERFILE 2>/dev/null
        rc=$?
        if test $rc -eq 0; then
    	        AC_MSG_RESULT([yes])
    	        $2
        else
                AC_MSG_RESULT([no])
                $3
        fi
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
        darwin*) darwin="yes" ;;
        linux*) linux="yes" ;;
esac

AM_CONDITIONAL([DARWIN], [test x$darwin = xyes])
if test x$darwin = xyes; then
        DARWIN=y
else
        DARWIN=n
fi
AC_SUBST(DARWIN)

AM_CONDITIONAL([LINUX], [test x$linux = xyes])
if test x$linux = xyes; then
        LINUX=y
else
        LINUX=n
fi
AC_SUBST(LINUX)

if test x$linux = xyes; then
        AC_MSG_CHECKING([for linux sources dir])
        AC_ARG_WITH(linux,
                AC_HELP_STRING([--with-linux=path], [set path to linux sources]), 
                [KERNELDIR=$with_linux],
                [KERNELDIR=/lib/modules/`uname -r`/build]
        )

        AC_MSG_RESULT([$KERNELDIR])
        AC_SUBST(KERNELDIR)

        AC_MSG_CHECKING([for linux objects dir])
        AC_ARG_WITH(linux-obj,
                AC_HELP_STRING([--with-linux-obj=path], [set path to linux objects dir]), 
                [KERNELDIR_OBJ=$with_linux_obj],
                [KERNELDIR_OBJ=$KERNELDIR]
        )

        AC_MSG_RESULT([$KERNELDIR_OBJ])
        AC_SUBST(KERNELDIR_OBJ)
        
        KERNELMOD_DIR="/lib/modules/`uname -r`"
        AC_SUBST(KERNELMOD_DIR)

        # Example of checking for kernel symbol
        COLIBRI_LINUX_CHECK_SYMBOL([journal_abort], 
                [AC_DEFINE([HAVE_JOURNAL_ABORT], [], [Have journal_abort symbol])], [])
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

CFLAGS="$CFLAGS -D_REENTRANT"

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
if test x$linux = xyes -a x$enable_db4_dio = xyes; then
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

OLD_LIBS=$LIBS
LIBS=""
AC_SEARCH_LIBS(io_getevents, c aio, , [AC_MSG_ERROR(io_getevents cannot be found!)])
AIO_LIBS=$LIBS
AC_SUBST(AIO_LIBS)
LIBD=$OLD_LIBS

AC_CHECK_DECL(mallinfo, [AC_DEFINE(HAVE_MALLINFO)], [], [[#include <malloc.h>]])
AC_CHECK_DECL(malloc_size, [AC_DEFINE(HAVE_MALLOC_SIZE)], [], [[#include <malloc/malloc.h>]])
AC_CHECK_DECL(backtrace, [AC_DEFINE(HAVE_BACKTRACE)], [], [[#include <execinfo.h>]])

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
                 rpc/ut/Makefile
                 net/Makefile
                 net/sunrpc/Makefile
                 net/ut/Makefile
                 nrs/Makefile
                 sns/Makefile
                 sm/Makefile
                 mds/Makefile
                 patches/Makefile
                 stob/Makefile
                 stob/ut/Makefile
                 utils/Makefile
                 utils/ploss/Makefile
                ])
AC_OUTPUT
