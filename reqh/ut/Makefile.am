UT_SRCDIR          = @SRCDIR@/reqh/ut

noinst_LTLIBRARIES = libreqh-ut.la
INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

libreqh_ut_la_SOURCES = reqh_fom_ut.c fom_io.ff

EXTRA_DIST = fom_io.ff

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr fom_io_[uk].[ch]
