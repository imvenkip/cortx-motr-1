INCLUDES             = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

noinst_PROGRAMS      = bulkping

bulkping_SOURCES     = main.c ping.c ping.h
bulkping_LDADD       = $(top_builddir)/colibri/libcolibri.la \
                       $(top_builddir)/ut/libcolibri-ut.la
