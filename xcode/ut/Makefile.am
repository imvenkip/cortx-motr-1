xcodeutdir        = $(includedir)/xcode/ut
noinst_LTLIBRARIES   = libxcode-ut.la

libxcode_ut_la_SOURCES  = xcode_fops.h xcode_fops.c xcode_fop_test.c xcode_test.c \
			  xcode.c ff2c.c

xcode_fops.h xcode_fops.c : xcode_fops.ff \
	$(top_builddir)/xcode/ff2c/ff2c
	$(top_builddir)/xcode/ff2c/ff2c $<

EXTRA_DIST = xcode_fops.ff

clean-local:
	cd @SRCDIR@/xcode/ut ; \
	rm -fr xcode_fops.[ch]
