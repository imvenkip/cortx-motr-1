INCLUDES		= -I. -I$(top_srcdir) -I$(top_srcdir)/include

noinst_PROGRAMS		= bulkio

bulkio_SOURCES		= bulkio_main.c bulkio_client.c bulkio_server.c
bulkio_LDADD		= $(top_builddir)/colibri/libcolibri.la \
			  $(top_builddir)/ut/libcolibri-ut.la
