include $(top_srcdir)/Makefile.ff

noinst_LTLIBRARIES       = libcolibri-ut.la

INCLUDES                 = -iquote . -iquote $(top_srcdir) -iquote $(top_srcdir)/include

CS_TEST_FOPS             = cs_test_fops_ff.h cs_test_fops_ff.c

EXTRA_DIST               = cs_test_fops.ff

libcolibri_ut_la_SOURCES = ut.h ut.c rpc.h rpc.c      \
			   $(CS_TEST_FOPS)            \
			   cs_service.c cs_service.h  \
			   cs_fop_foms.h cs_fop_foms.c

MOSTLYCLEANFILES         = $(CS_TEST_FOPS)
