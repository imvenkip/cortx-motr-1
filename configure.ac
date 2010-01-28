AC_PREREQ(2.59)
AC_INIT(colibri, 0.1, colibri-support@clusterstor.com)
AC_CONFIG_SRCDIR([include/colibri/colibri.h])
AC_CONFIG_HEADER([config.h])

AH_TEMPLATE([PACKAGE], [Package name.])
AH_TEMPLATE([VERSION], [Version of the package.])
AH_TEMPLATE([ENABLE_DEBUG], [Enable debug info.])
AH_TEMPLATE([ENABLE_GSSRPC], [Enable gssrpc.])

PACKAGE=colibri
VERSION=0.1

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

# Checks for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_AWK
AC_PROG_LIBTOOL

# Checks for libraries.
AC_SEARCH_LIBS(xdr_domainname, c nsl, , [AC_MSG_ERROR(xdr_domainname cannot be found!)])
AC_SEARCH_LIBS(pthread_create, c pthread, , [AC_MSG_ERROR(pthread_create cannot be found!)])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h pthread.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MALLOC

# Configure options
AC_ARG_ENABLE(debug,
        [--disable-debug        disable debug information], , 
            enable_debug=yes
)

AC_ARG_ENABLE(gssrpc,
        [--disable-gssrpc       disable gssrpc],
            disable_gssrpc=yes
)

AC_ARG_WITH(db4,
    	[--with-db4             prefix of where db4 was built], ,
)

OLD_LIBS=$LIBS
OLD_CFLAGS=$CFLAGS
LIBS=""
CFLAGS=""

if test x$with_db4 != x; then
	DB4SRC="$with_db4"
	if ! test -f $DB4SRC/build_unix/libdb-4.8.la; then
		AC_MSG_ERROR($DB4SRC/build_unix/libdb-4.8.la not found! Did you build db-4?)
	fi
	DB4LIBS="$DB4SRC/build_unix/libdb-4.8.la"
	AC_SUBST(DB4LIBS)
	AC_SUBST(DB4SRC)
else
	AC_MSG_ERROR(Cannot build mds server without --with-db4)
fi

LIBS=$OLD_LIBS
CFLAGS=$OLD_CFLAGS

if test x$enable_gssrpc = xyes; then
	AC_CHECK_LIB(gssrpc, xdr_int, , 
		AC_MSG_ERROR(libgssrpc cannot be found!)
	)

	AC_CHECK_HEADERS([gssrpc/pmap_clnt.h gssrpc/rpc.h], , 
		AC_MSG_ERROR(gssrpc/pmap_clnt.h gssrpc/rpc.h cannot be found!)
	)
	AC_DEFINE(ENABLE_GSSRPC)
fi

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
	AC_ARG_WITH(linux,
    		[--with-linux           prefix of where linux was built], ,
	)

	if test x$with_linux != x; then
		KERNELDIR=$with_linux
	else
		KERNELDIR="/lib/modules/`uname -r`/build"
	fi
	AC_SUBST(KERNELDIR)
fi

if test x$enable_debug = xyes; then
        CFLAGS="$CFLAGS -O0 -g"
        KCFLAGS="-O0 -g"
        AC_DEFINE(ENABLE_DEBUG)
else
        KCFLAGS="-O2"
	CFLAGS="$CFLAGS -O2"
fi
AC_SUBST(KCFLAGS)

SRCDIR=$(cd $srcdir; echo $PWD)
AC_SUBST(SRCDIR)

AC_CONFIG_FILES([
		 Makefile
                 doc/Makefile
                 include/Makefile
                 include/colibri/Makefile
                 man/Makefile
                 src/Makefile
                 src/addb/Makefile
                 src/ctdb/Makefile
                 src/fol/Makefile
                 src/lib/Makefile
                 src/mds/Makefile
                 src/mds/kernel/Makefile
                 src/net/Makefile
                 src/nrs/Makefile
                 src/sns/Makefile
                 tests/Makefile])
AC_OUTPUT
