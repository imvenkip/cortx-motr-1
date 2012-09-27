include $(top_srcdir)/Makefile.ff

ioservicedir			= $(includedir)/colibri/ioservice
ioservice_HEADERS		= io_foms.h io_fops.h io_fops_ff.h        \
				  cobfid_map.h cob_foms.h io_device.h

IO_FOPS_SOURCES                 = io_fops_ff.h io_fops_ff.c

noinst_LTLIBRARIES		= libcolibri-ioservice.la
libcolibri_ioservice_la_SOURCES	= $(IO_FOPS_SOURCES)                      \
				  io_foms.c io_foms.h io_fops.c io_fops.h \
                                  io_service.h io_service.c \
                                  cobfid_map.c cobfid_map.h cob_foms.c \
				  io_device.h io_device.c

INCLUDES			= -iquote . -iquote $(top_srcdir) -iquote \
				  $(top_srcdir)/include

EXTRA_DIST			= io_fops.ff

MOSTLYCLEANFILES                = $(IO_FOPS_SOURCES)
