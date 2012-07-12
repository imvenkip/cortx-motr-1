noinst_LTLIBRARIES = libfop-lock-ut.la
INCLUDES           = -iquote . -iquote $(top_srcdir)/include -iquote $(top_srcdir)

rdwr_fop_u.h rdwr_fop_u.c: \
	rdwr_fop.ff $(top_builddir)/fop/rt/libc2rt.la \
	$(top_builddir)/fop/fop2c
	$(top_builddir)/fop/fop2c -u $<

libfop_lock_ut_la_SOURCES  = rdwr_fop_u.h rdwr_fop_u.c rdwr_fop.h rdwr_fop.c \
			     rdwr_fom.h rdwr_fom.c \
			     rdwr_test_bench.h rdwr_test_bench.c \
			     long_lock_ut.c

EXTRA_DIST = rdwr_fop.ff

clean-local:
	cd @SRCDIR@/fop/ut/long_lock ; \
	rm -fr rdwr_fop_u.[ch]
