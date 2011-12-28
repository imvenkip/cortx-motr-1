INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = bulkping

bulkping_SOURCES     = main.c ping.c
bulkping_LDADD       = $(top_builddir)/colibri/libcolibri.la \
                       $(top_builddir)/ut/libcolibri-ut.la
