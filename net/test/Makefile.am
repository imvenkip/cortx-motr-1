noinst_LTLIBRARIES		= libcolibri-net-test.la
noinst_PROGRAMS			= net_test_console net_test_node

INCLUDES			= -I. -I$(top_srcdir) -I$(top_srcdir)/include

net_test_SRC			= client.c commands.c network.c node_config.c \
				  node_main.c server.c stats.c
net_test_HDR			= client.h commands.h network.h node_config.h \
				  node_main.h server.h stats.h

libcolibri_net_test_la_SOURCES	= $(net_test_HDR) $(net_test_SRC)

net_test_console_SOURCES	= user_space/console.c \
				  user_space/console_config.c \
				  user_space/console_config.h
net_test_console_LDADD		= $(top_builddir)/colibri/libcolibri.la


net_test_node_SOURCES		= user_space/node.c \
				  user_space/node_config_u.c
				  user_space/node_config_u.h
net_test_node_LDADD		= $(top_builddir)/colibri/libcolibri.la
