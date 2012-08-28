INCLUDES			= -iquote . -iquote $(top_srcdir)       \
				  -iquote $(top_srcdir)/include

console.c:
	ln -sf $(top_builddir)/console/console.c .

noinst_PROGRAMS			= c2console

c2console_SOURCES		= console.c

c2console_LDADD			= $(top_builddir)/colibri/libcolibri.la \
				  $(top_builddir)/ut/libcolibri-ut.la

MOSTLYCLEANFILES                = console.c
