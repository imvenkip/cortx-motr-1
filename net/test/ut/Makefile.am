noinst_LTLIBRARIES	= libnet-test-ut.la

INCLUDES	= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

libnet_test_ut_la_SOURCES = client_server.c commands.c main.c network.c \
			    ringbuf.c serialize.c slist.c stats.c	\
			    uint256.c
libnet_test_ut_la_LIBADD  = $(top_builddir)/colibri/libcolibri.la
