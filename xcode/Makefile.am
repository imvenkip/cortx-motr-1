xcodedir                     = $(includedir)/colibri/xcode
xcode_HEADERS                = bufvec_xcode.h xcode.h

noinst_LTLIBRARIES           = libcolibri-xcode.la
libcolibri_xcode_la_SOURCES  = bufvec_xcode.c bufvec_xcode.h xcode.c xcode.h

EXTRA_DIST                   = cursor.c xcode_attr.h gccxml2xcode

INCLUDES                     = -iquote . -iquote $(top_srcdir) \
				-iquote $(top_srcdir)/include

