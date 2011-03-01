INCLUDES           = -I. -I$(top_srcdir)/include -I$(top_srcdir)
UT_SRCDIR          = @SRCDIR@/fop/ut

noinst_LTLIBRARIES = libfop-ut.la

test_format_u.h test_format_u.c test_format_k.h test_format_k.c: \
                                     test_format.ff \
                                     $(top_builddir)/colibri/libcolibri.la \
                                     $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -ku $<

iterator_test_u.h iterator_test_u.c: \
                                     iterator_test.ff \
                                     $(top_builddir)/colibri/libcolibri.la \
                                     $(top_builddir)/fop/fop2c 
	$(top_builddir)/fop/fop2c -u $<

libfop_ut_la_SOURCES = 	iterator_test_u.h iterator_test_u.c \
			test_format_u.h test_format_u.c \
			fmt_test.c test_format.ff \
			iterator_test.c

clean-local:
	cd $(UT_SRCDIR) ; \
	rm -fr test_format_[uk].[ch]
	rm -fr iterator_test_u.[ch]

distdir:
	cp $(UT_SRCDIR)/Makefile.am $(UT_SRCDIR)/Makefile.in \
           $(UT_SRCDIR)/fmt_test.c $(UT_SRCDIR)/test_format.ff \
           @top_builddir@/@PACKAGE@-@VERSION@/fop/ut
