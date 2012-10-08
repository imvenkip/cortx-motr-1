xcodedir                     = $(includedir)/colibri/xcode
xcode_HEADERS                = xcode.h

noinst_LTLIBRARIES           = libcolibri-xcode.la
libcolibri_xcode_la_SOURCES  = string.c xcode.c xcode.h

EXTRA_DIST                   = cursor.c xcode_attr.h gccxml2xcode

INCLUDES                     = -iquote . -iquote $(top_srcdir) \
			       -iquote $(top_srcdir)/include
