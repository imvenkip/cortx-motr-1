lut_helperdir         = $(includedir)/colibri/net/lnet/ut
noinst_LTLIBRARIES    = liblnet-ut.la

INCLUDES              = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

liblnet_ut_la_SOURCES = lnet_ut.c
liblnet_ut_la_LIBADD  = $(top_builddir)/colibri/libcolibri.la

noinst_PROGRAMS       = lut_helper

lut_helper_SOURCES    = lut_main.c
lut_helper_HEADERS    = lnet_drv_ut.h
lut_helper_LDADD      = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST            = linux_kernel/klnet_ut.c
