lut_helperdir       = $(includedir)/colibri/net/lnet/ut

INCLUDES            = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS     = lut_helper

lut_helper_SOURCES  = lut_main.c
lut_helper_HEADERS  = lnet_drv_ut.h
lut_helper_LDADD    = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST          = lnet_ut.c linux_kernel/klnet_ut.c
