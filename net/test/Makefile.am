noinst_LTLIBRARIES		= libcolibri-net-test.la

INCLUDES			= -I. -I$(top_srcdir) -I$(top_srcdir)/include

net_test_SRC			= commands.c console.c network.c node_bulk.c  \
				  node.c node_ping.c ringbuf.c serialize.c    \
				  service.c slist.c stats.c str.c	      \
				  user_space/stats_u.c
net_test_HDR			= commands.h console.h network.h node_bulk.h  \
				  node.h node_ping.h ringbuf.h serialize.h    \
				  service.h slist.h stats.h str.h	      \
				  user_space/stats_u.h

libcolibri_net_test_la_SOURCES	= $(net_test_HDR) $(net_test_SRC)

EXTRA_DIST			= test-user.sh.in
