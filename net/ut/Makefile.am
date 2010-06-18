INCLUDES             = -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS      = connection client kclient
# utils
# client server 

connection_SOURCES   = connection.c
connection_LDADD     = $(top_builddir)/colibri/libcolibri.la

#utils_SOURCES       = connection.c
#utils_LDADD         = $(top_builddir)/colibri/libcolibri.la


client_SOURCES       = client.c
client_LDFLAGS       = -pthread
client_LDADD         = $(top_builddir)/colibri/libcolibri.la

#rpc-server_SOURCES  = rpc-server.c
#rpc-server_LDADD    = $(top_builddir)/colibri/libcolibri.la

kclient_SOURCES      = kclient.c
kclient_LDADD        =
