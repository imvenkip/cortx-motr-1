ioservicedir			= $(includedir)/colibri/ioservice
ioservice_HEADERS		= io_foms.h io_fops.h io_fops_xc.h        \
				  cobfid_map.h cob_foms.h

IO_FOPS_SOURCES                 = io_fops_xc.h io_fops_xc.c

$(IO_FOPS_SOURCES): io_fops_xc.ff \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

noinst_LTLIBRARIES		= libcolibri-ioservice.la
libcolibri_ioservice_la_SOURCES	= $(IO_FOPS_SOURCES)                      \
				  io_foms.c io_foms.h io_fops.c io_fops.h \
                                  io_service.h io_service.c               \
                                  cobfid_map.c cobfid_map.h cob_foms.c

INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote \
				  $(top_srcdir)/include

EXTRA_DIST			= io_fops_xc.ff

MOSTLYCLEANFILES                = $(IO_FOPS_SOURCES)
