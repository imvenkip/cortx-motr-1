include $(top_srcdir)/Makefile.xc

xcodeutdir              = $(includedir)/xcode/ut
noinst_LTLIBRARIES      = libxcode-ut.la

libxcode_ut_la_SOURCES  = $(XCODE_FOPS) xcode_fop_test.c xcode_test.c \
			  xcode.c ff2c.c test_gccxml_simple_xc.c      \
			  test_gccxml_xc.c

XCODE_FOPS              = xcode_fops.c xcode_fops.h

$(XCODE_FOPS) : xcode_fops.ff \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

EXTRA_DIST              = xcode_fops.ff test_gccxml_simple.h    \
			  test_gccxml_simple_xc.h test_gccxml.h \
			  test_gccxml_xc.h

MOSTLYCLEANFILES        = $(XCODE_FOPS)

clean-local: clean-xc
