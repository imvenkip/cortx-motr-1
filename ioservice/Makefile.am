ioservicedir			= $(includedir)/colibri/ioservice
ioservice_HEADERS		= io_foms.h io_fops.h io_fops_u.h io_fops_k.h cobfid_map.h cob_foms.h

io_fops_u.h io_fops_u.c: io_fops.ff \
                  $(top_builddir)/fop/rt/libc2rt.la \
                  $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -uk $<

noinst_LTLIBRARIES		= libcolibri-ioservice.la
libcolibri_ioservice_la_SOURCES	= io_fops_u.h io_fops_u.c \
				  io_foms.c io_foms.h io_fops.c io_fops.h \
                                  io_service.h io_service.c \
                                  cobfid_map.c cobfid_map.h cob_foms.c

INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

EXTRA_DIST			= io_fops.ff

clean-local:
	rm -rf io_fops_[uk]*.[ch]
