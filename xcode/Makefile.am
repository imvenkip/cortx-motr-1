xcodedir                     = $(includedir)/colibri/xcode
xcode_HEADERS                = bufvec_xcode.h xcode.h

noinst_LTLIBRARIES           = libcolibri-xcode.la
libcolibri_xcode_la_SOURCES  = bufvec_xcode.c bufvec_xcode_fop.c \
			       bufvec_xcode_size.c bufvec_xcode.h \
			       xcode.c xcode.h

EXTRA_DIST                   = cursor.c

INCLUDES                     = -I. -I$(top_srcdir) -I$(top_srcdir)/include

