io_fop_u.h io_fop_u.c : io_fop.ff \
                        $(top_builddir)/fop/rt/libc2rt.la \
                        $(top_builddir)/fop/fop2c
			$(top_builddir)/fop/fop2c -u $<

EXTRA_DIST = io_fop.ff

libreqh_ut_la_SOURCES   = reqh_fom_ut.c io_fop.c io_fop.h io_fop_u.c

reqh_fom_ut.c : io_fop_u.h

io_fop_u.h : io_fop.ff

INCLUDES                = -I. -I$(top_srcdir)/include -I$(top_srcdir)

noinst_LTLIBRARIES      = libreqh-ut.la
