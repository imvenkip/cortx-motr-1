include $(top_srcdir)/Makefile.xc
include $(top_srcdir)/Makefile.ff

xcodeutdir              = $(includedir)/xcode/ut
noinst_LTLIBRARIES      = libxcode-ut.la

libxcode_ut_la_SOURCES  = $(XCODE_FOPS) xcode_fop_test.c xcode.c ff2c.c \
			   test_gccxml_simple_xc.c test_gccxml_xc.c

XCODE_FOPS              = xcode_fops_ff.c xcode_fops_ff.h

EXTRA_DIST              = xcode_fops.ff test_gccxml_simple.h    \
			  test_gccxml_simple_xc.h test_gccxml.h \
			  test_gccxml_xc.h

MOSTLYCLEANFILES        = $(XCODE_FOPS)

clean-local: clean-xc
