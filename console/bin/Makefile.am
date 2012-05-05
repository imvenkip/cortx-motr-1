INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

MOSTLYCLEANFILES                = main.c
main.c:
	ln -sf $(top_builddir)/console/main.c main.c

noinst_PROGRAMS			= c2console

c2console_SOURCES		= main.c

c2console_LDADD			= $(top_builddir)/colibri/libcolibri.la \
				  $(top_builddir)/ut/libcolibri-ut.la
