noinst_LTLIBRARIES		= libcolibri-net-test.la

INCLUDES			= -I. -I$(top_srcdir) -I$(top_srcdir)/include

net_test_SRC			= client.c commands.c network.c node_config.c \
				  node_main.c ringbuf.c serialize.c server.c  \
				  slist.c stats.c
net_test_HDR			= client.h commands.h network.h node_config.h \
				  node_main.h ringbuf.h serialize.h server.h  \
				  slist.h stats.h

libcolibri_net_test_la_SOURCES	= $(net_test_HDR) $(net_test_SRC)
