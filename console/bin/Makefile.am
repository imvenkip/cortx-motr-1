INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

MOSTLYCLEANFILES                = main.c
main.c:
	ln -sf $(top_builddir)/console/main.c main.c

net.c:
	ln -sf $(top_builddir)/ut/net.c net.c

noinst_PROGRAMS			= c2console

c2console_SOURCES		= main.c net.c

c2console_LDADD			= $(top_builddir)/colibri/libcolibri.la \
				  $(top_builddir)/ut/libcolibri-ut.la

clean:
	cd $(top_builddir)/console/bin;
	rm -f main.c net.c
