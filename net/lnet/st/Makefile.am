INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = lnetping

lnetping_SOURCES     = main.c ping.c ping.h
lnetping_LDADD       = $(top_builddir)/colibri/libcolibri.la \
                       $(top_builddir)/ut/libcolibri-ut.la

EXTRA_DIST           = linux_kernel/main.c \
                       linux_kernel/test.sh
