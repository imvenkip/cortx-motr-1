noinst_LTLIBRARIES       = libcolibri-ut.la

INCLUDES                 = -I. -I$(top_srcdir) -I$(top_srcdir)/include

cs_test_fops_u.h cs_test_fops_u.c : cs_test_fops.ff \
				$(top_builddir)/fop/rt/libc2rt.la \
				$(top_builddir)/fop/fop2c
		$(top_builddir)/fop/fop2c -u $<

EXTRA_DIST = cs_test_fops.ff

# libcolibri_ut_la_CFLAGS  = -save-temps
libcolibri_ut_la_SOURCES = ut.h rpc.h rpc.c \
			   cs_test_fops_u.h cs_test_fops_u.c \
			   cs_service.c cs_service.h \
			   cs_fop_foms.h cs_fop_foms.c \
			   net.h net.c

clean-local:
	cd @SRCDIR@/ut ; \
	rm -f cs_test_fops_u.[ch]
