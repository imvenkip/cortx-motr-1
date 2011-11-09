UT_SRCDIR          = @SRCDIR@/colibri/ut

noinst_LTLIBRARIES = libcs-ut.la
INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)

cs_test_fops_u.h cs_test_fops_u.c : cs_test_fops.ff \
				$(top_builddir)/fop/rt/libc2rt.la \
				$(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -u $<

libcs_ut_la_SOURCES = cs_test_fops_u.h cs_test_fops_u.c \
                        cs_test_fops.ff cs_ut_service.c \
			cs_ut_fop_foms.h cs_ut_fop_foms.c \
			cs_ut_main.c

libcs_ut_la_LIBADD = $(top_builddir)/colibri/libcolibri.la

EXTRA_DIST = cs_test_fops.ff

clean-local:
	rm -f cs_test_fops_u.[ch]
