
# Checking for CUnit
AC_DEFUN([XY_CHECK_CUNIT],[
  AH_TEMPLATE([HAVE_CUNIT], [Have cunit library])

  OLD_CFLAGS=$CFLAGS
  CFLAGS="$CFLAGS -I/usr/local/include -I/opt/local/include"
  OLD_LIBS=$LIBS
  LIBS="$LIBS -L/usr/local/lib -L/opt/local/lib"

  CUNIT_LIBS=""
  enable_cunit=yes
  notfound="cannot be found!  See http://cunit.sf.net"

  # Make sure that CUnit is found on Mac OS X where it is usually
  # instalelled from ports to /opt/local
  AC_CHECK_HEADERS([CUnit/CUnit.h CUnit/Basic.h CUnit/Automated.h], [], [
    AC_MSG_NOTICE([CUnit headers $notfound])
    enable_cunit=no
    break
  ])

  if test x$enable_cunit = xyes; then
    # Make sure that CUnit is found on Mac OS X where it is usually
    # instalelled from ports to /opt/local
    AC_SEARCH_LIBS([CU_basic_run_tests], [cunit], [], [
      AC_MSG_NOTICE([CU_basic_run_tests() $notfound])
      enable_cunit=no
    ])
    if test x$enable_cunit = xyes; then
      CUNIT_LIBS=$LIBS
      AC_DEFINE([HAVE_CUNIT], 1)
    fi
  fi

  LIBS=$OLD_LIBS
  CFLAGS=$OLD_CFLAGS
  AC_SUBST([CUNIT_LIBS])
  AM_CONDITIONAL([HAVE_CUNIT_SUPPORT], [test x$enable_cunit = xyes])
])
