include $(top_srcdir)/Makefile.ff

IO_FOPS                 = io_fop_ff.h io_fop_ff.c

EXTRA_DIST              = io_fop.ff

libreqh_ut_la_SOURCES   = reqh_fom_ut.c io_fop.c io_fop.h io_fop_ff.c

reqh_fom_ut.c : io_fop_ff.h

INCLUDES                = -iquote . -iquote $(top_srcdir)/include -iquote $(top_srcdir)

noinst_LTLIBRARIES      = libreqh-ut.la

MOSTLYCLEANFILES        = $(IO_FOPS)
