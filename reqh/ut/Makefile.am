UT_SRCDIR          = @SRCDIR@/reqh/ut

noinst_LTLIBRARIES = libreqh-ut.la
INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

fom_io_u.h fom_io_u.c : fom_io.ff \
                        $(top_builddir)/fop/rt/libc2rt.la \
                        $(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

libreqh_ut_la_SOURCES = fom_io_u.h fom_io_u.c \
			reqh_fom_ut.c fom_io.ff

EXTRA_DIST = fom_io.ff

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr fom_io_[uk].[ch]
