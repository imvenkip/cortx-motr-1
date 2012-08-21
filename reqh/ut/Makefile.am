IO_FOPS                 = io_fop_xc.h io_fop_xc.c
$(IO_FOPS) : io_fop_xc.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

EXTRA_DIST              = io_fop_xc.ff

libreqh_ut_la_SOURCES   = reqh_fom_ut.c io_fop.c io_fop.h io_fop_xc.c

reqh_fom_ut.c : io_fop_xc.h

INCLUDES                = -iquote . -iquote $(top_srcdir)/include -iquote $(top_srcdir)

noinst_LTLIBRARIES      = libreqh-ut.la

MOSTLYCLEANFILES        = $(IO_FOPS)
