noinst_LTLIBRARIES    = libconf-ut.la
libconf_ut_la_SOURCES = confc.c main.c obj.c reg.c xread.c
libconf_ut_la_LIBADD  = $(top_builddir)/colibri/libcolibri.la
libconf_ut_la_CFLAGS = \
    -DCOLIBRI_CONFX_OBJ_CFG=$(abs_top_builddir)/conf/ut/conf_xc.txt \
    -DCOLIBRI_CONFX_OBJ_CFG_XC=$(abs_top_builddir)/conf/ut/conf_xc.txt
