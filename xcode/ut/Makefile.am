xcodeutdir              = $(includedir)/xcode/ut
noinst_LTLIBRARIES      = libxcode-ut.la

libxcode_ut_la_SOURCES  = $(XCODE_FOPS) xcode_fop_test.c \
			  xcode_test.c xcode.c ff2c.c

XCODE_FOPS              = xcode_fops.c xcode_fops.h

$(XCODE_FOPS) : xcode_fops.ff
	$(top_builddir)/xcode/ff2c/ff2c $<

EXTRA_DIST              = xcode_fops.ff

MOSTLYCLEANFILES        = $(XCODE_FOPS)
